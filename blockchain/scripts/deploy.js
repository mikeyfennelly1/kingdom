const { ethers } = require("hardhat");

async function main() {
  const [deployer] = await ethers.getSigners();
  console.log("Deploying with account:", deployer.address);

  const MessageIntegrity = await ethers.getContractFactory("MessageIntegrity");
  const contract = await MessageIntegrity.deploy();
  await contract.waitForDeployment();

  const address = await contract.getAddress();
  console.log("MessageIntegrity deployed to:", address);
  console.log("Add this to your .env: CONTRACT_ADDRESS=" + address);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
