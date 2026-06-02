const { expect } = require("chai");
const { ethers } = require("hardhat");
const { anyValue } = require("@nomicfoundation/hardhat-chai-matchers/withArgs");

// Build a Merkle root from an array of bytes32 leaf hashes.
// Leaves are sorted pairwise before hashing so order within a pair doesn't matter.
// Odd-length levels duplicate the last leaf.
function buildMerkleRoot(leaves) {
  if (leaves.length === 0) throw new Error("empty leaves");
  let level = [...leaves];
  while (level.length > 1) {
    if (level.length % 2 !== 0) level.push(level[level.length - 1]);
    const next = [];
    for (let i = 0; i < level.length; i += 2) {
      const [a, b] = [level[i], level[i + 1]].sort();
      next.push(ethers.keccak256(ethers.concat([a, b])));
    }
    level = next;
  }
  return level[0];
}

describe("MessageIntegrity", function () {
  let contract;
  let owner;

  beforeEach(async function () {
    [owner] = await ethers.getSigners();
    const MessageIntegrity = await ethers.getContractFactory("MessageIntegrity");
    contract = await MessageIntegrity.deploy();
    await contract.waitForDeployment();
  });

  it("emits BatchRecorded event with correct root, messageCount and leaves", async function () {
    const leaves = [
      ethers.keccak256(ethers.toUtf8Bytes("msg 1")),
      ethers.keccak256(ethers.toUtf8Bytes("msg 2")),
    ];
    const root = buildMerkleRoot(leaves);

    await expect(contract.recordBatch(root, leaves))
      .to.emit(contract, "BatchRecorded")
      .withArgs(0, root, anyValue, leaves.length, leaves);
  });

  it("stores batch root and timestamp after recordBatch", async function () {
    const leaves = [ethers.keccak256(ethers.toUtf8Bytes("hello"))];
    const root = buildMerkleRoot(leaves);

    await contract.recordBatch(root, leaves);

    const batch = await contract.batches(0);
    expect(batch.root).to.equal(root);
    expect(batch.timestamp).to.be.greaterThan(0);
    expect(batch.messageCount).to.equal(1);
  });

  it("increments nextBatchId with each batch", async function () {
    const leaf = ethers.keccak256(ethers.toUtf8Bytes("a"));
    const root = buildMerkleRoot([leaf]);

    await contract.recordBatch(root, [leaf]);
    await contract.recordBatch(root, [leaf]);

    expect(await contract.nextBatchId()).to.equal(2);
  });

  it("stores multiple batches independently", async function () {
    const leaves1 = [ethers.keccak256(ethers.toUtf8Bytes("batch 1 msg"))];
    const leaves2 = [
      ethers.keccak256(ethers.toUtf8Bytes("batch 2 msg a")),
      ethers.keccak256(ethers.toUtf8Bytes("batch 2 msg b")),
    ];
    const root1 = buildMerkleRoot(leaves1);
    const root2 = buildMerkleRoot(leaves2);

    await contract.recordBatch(root1, leaves1);
    await contract.recordBatch(root2, leaves2);

    expect((await contract.batches(0)).root).to.equal(root1);
    expect((await contract.batches(1)).root).to.equal(root2);
  });

  it("reverts with EmptyBatch when no leaves provided", async function () {
    const root = ethers.keccak256(ethers.toUtf8Bytes("anything"));

    await expect(
      contract.recordBatch(root, [])
    ).to.be.revertedWithCustomError(contract, "EmptyBatch");
  });

  it("reverts when called by non-owner", async function () {
    const [, nonOwner] = await ethers.getSigners();
    const leaf = ethers.keccak256(ethers.toUtf8Bytes("test"));
    const root = buildMerkleRoot([leaf]);

    await expect(
      contract.connect(nonOwner).recordBatch(root, [leaf])
    ).to.be.revertedWithCustomError(contract, "Unauthorized");
  });
});
