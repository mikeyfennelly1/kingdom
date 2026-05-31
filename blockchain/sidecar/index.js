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
  "function recordHash(uint256 conversationId, uint256 msgId, bytes32 hash) external",
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
 * Body: { conversationId: number, msgId: number, ciphertext: string }
 * Response: { txHash: string }
 */
app.post("/record", async (req, res) => {
  const { conversationId, msgId, ciphertext } = req.body;

  if (conversationId === undefined || msgId === undefined || !ciphertext) {
    return res.status(400).json({ error: "conversationId, msgId and ciphertext are required" });
  }

  const hash = ethers.keccak256(ethers.toUtf8Bytes(ciphertext));

  try {
    const tx = await contract.recordHash(conversationId, msgId, hash);
    // Respond immediately with the tx hash — do not block on mining confirmation.
    res.json({ txHash: tx.hash });
    // Confirm mining in the background.
    tx.wait()
      .then(() => console.log(`Confirmed on-chain: conversation ${conversationId} msg ${msgId} hash ${hash} (tx: ${tx.hash})`))
      .catch((err) => console.error(`Mining failed for tx ${tx.hash}:`, err.message));
  } catch (err) {
    console.error(`Failed to record hash for conversation ${conversationId}:`, err.message);
    res.status(500).json({ error: "failed to record hash on-chain" });
  }
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
