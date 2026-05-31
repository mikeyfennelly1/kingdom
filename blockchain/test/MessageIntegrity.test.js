const { expect } = require("chai");
const { ethers } = require("hardhat");
const { anyValue } = require("@nomicfoundation/hardhat-chai-matchers/withArgs");

describe("MessageIntegrity", function () {
  let contract;
  let owner;

  beforeEach(async function () {
    [owner] = await ethers.getSigners();
    const MessageIntegrity = await ethers.getContractFactory("MessageIntegrity");
    contract = await MessageIntegrity.deploy();
    await contract.waitForDeployment();
  });

  it("emits HashRecorded event with correct conversationId, msgId and hash", async function () {
    const conversationId = 1;
    const msgId = 10;
    const hash = ethers.keccak256(ethers.toUtf8Bytes("test ciphertext"));

    await expect(contract.recordHash(conversationId, msgId, hash))
      .to.emit(contract, "HashRecorded")
      .withArgs(conversationId, msgId, hash, anyValue);
  });

  it("stores hash in nested mapping after recordHash", async function () {
    const conversationId = 42;
    const msgId = 7;
    const hash = ethers.keccak256(ethers.toUtf8Bytes("hello world"));

    await contract.recordHash(conversationId, msgId, hash);

    expect(await contract.hashes(conversationId, msgId)).to.equal(hash);
  });

  it("stores timestamp in nested mapping after recordHash", async function () {
    const conversationId = 7;
    const msgId = 3;
    const hash = ethers.keccak256(ethers.toUtf8Bytes("some message"));

    await contract.recordHash(conversationId, msgId, hash);

    const timestamp = await contract.timestamps(conversationId, msgId);
    expect(timestamp).to.be.greaterThan(0);
  });

  it("stores multiple messages in the same conversation independently", async function () {
    const conversationId = 1;
    const hash1 = ethers.keccak256(ethers.toUtf8Bytes("first message"));
    const hash2 = ethers.keccak256(ethers.toUtf8Bytes("second message"));

    await contract.recordHash(conversationId, 1, hash1);
    await contract.recordHash(conversationId, 2, hash2);

    expect(await contract.hashes(conversationId, 1)).to.equal(hash1);
    expect(await contract.hashes(conversationId, 2)).to.equal(hash2);
  });

  it("reverts when called by non-owner", async function () {
    const [, nonOwner] = await ethers.getSigners();
    const hash = ethers.keccak256(ethers.toUtf8Bytes("test"));

    await expect(
      contract.connect(nonOwner).recordHash(1, 1, hash)
    ).to.be.revertedWithCustomError(contract, "Unauthorized");
  });
});
