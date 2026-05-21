import "dotenv/config";
import express from "express";
import { ethers } from "ethers";

const { SEPOLIA_RPC_URL, PRIVATE_KEY, CONTRACT_ADDRESS, PORT = 3001 } = process.env;

if (!SEPOLIA_RPC_URL || !PRIVATE_KEY || !CONTRACT_ADDRESS) {
  console.error("Missing required env vars: SEPOLIA_RPC_URL, PRIVATE_KEY, CONTRACT_ADDRESS");
  process.exit(1);
}

// Minimal ABI — only the function we call
const ABI = [
  "function recordHash(uint256 conversationId, bytes32 hash) external",
];

const provider = new ethers.JsonRpcProvider(SEPOLIA_RPC_URL);
const wallet = new ethers.Wallet(PRIVATE_KEY, provider);
const contract = new ethers.Contract(CONTRACT_ADDRESS, ABI, wallet);

const app = express();
app.use(express.json());

/**
 * POST /record
 *
 * Called by the kds server when a new message is added to a conversation.
 * Computes keccak256 of the ciphertext and records it on-chain.
 *
 * Body: { conversationId: number, ciphertext: string }
 * Response: { txHash: string }
 */
app.post("/record", async (req, res) => {
  const { conversationId, ciphertext } = req.body;

  if (conversationId === undefined || !ciphertext) {
    return res.status(400).json({ error: "conversationId and ciphertext are required" });
  }

  const hash = ethers.keccak256(ethers.toUtf8Bytes(ciphertext));

  const tx = await contract.recordHash(conversationId, hash);
  await tx.wait();

  console.log(`Recorded hash for conversation ${conversationId}: ${hash} (tx: ${tx.hash})`);
  res.json({ txHash: tx.hash });
});

/**
 * GET /health
 */
app.get("/health", (_req, res) => {
  res.json({ status: "ok" });
});

app.listen(PORT, () => {
  console.log(`Blockchain sidecar running on port ${PORT}`);
  console.log(`Contract: ${CONTRACT_ADDRESS}`);
});
