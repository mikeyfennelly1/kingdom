import "dotenv/config";
import crypto from "crypto";
import express from "express";
import { ethers } from "ethers";

const {
  SEPOLIA_RPC_URL,
  PRIVATE_KEY,
  CONTRACT_ADDRESS,
  PORT = 3001,
  BATCH_INTERVAL_MS = 5 * 60 * 1000, // 5 minutes
} = process.env;

if (!SEPOLIA_RPC_URL || !PRIVATE_KEY || !CONTRACT_ADDRESS) {
  console.error("Missing required env vars: SEPOLIA_RPC_URL, PRIVATE_KEY, CONTRACT_ADDRESS");
  process.exit(1);
}

const ABI = [
  "function recordBatch(bytes32 root, bytes32[] calldata leaves) external returns (uint256)",
  "event BatchRecorded(uint256 indexed batchId, bytes32 root, uint256 timestamp, uint256 messageCount, bytes32[] leaves)",
];

const provider = new ethers.JsonRpcProvider(SEPOLIA_RPC_URL);
const wallet = new ethers.Wallet(PRIVATE_KEY, provider);
const contract = new ethers.Contract(CONTRACT_ADDRESS, ABI, wallet);

// --- Merkle tree -----------------------------------------------------------

/**
 * Build a Merkle root from an array of bytes32 leaf hashes.
 * Pairs are sorted before hashing so the order within a pair doesn't matter
 * during verification. Odd-length levels duplicate the last leaf.
 */
function buildMerkleRoot(leaves) {
  let level = [...leaves];
  if (level.length === 1) return level[0];

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

// --- Queue and pending results ---------------------------------------------

// Each entry: { pendingId, conversationId, msgId, leaf }
const queue = [];

// pendingId → { status: "pending" | "confirmed" | "failed", txHash?, batchId?, error? }
const pendingResults = new Map();

// --- Batch flush -----------------------------------------------------------

async function flushBatch() {
  if (queue.length === 0) {
    console.log("Batch flush: queue empty, skipping.");
    return;
  }

  const batch = queue.splice(0, queue.length);
  const leaves = batch.map((entry) => entry.leaf);
  const root = buildMerkleRoot(leaves);

  console.log(`Flushing batch of ${batch.length} message(s). Root: ${root}`);

  try {
    const tx = await contract.recordBatch(root, leaves);
    console.log(`Batch tx submitted: ${tx.hash}`);

    const receipt = await tx.wait();

    // Parse the BatchRecorded event to get the batchId assigned on-chain.
    const iface = new ethers.Interface(ABI);
    let batchId = null;
    for (const log of receipt.logs) {
      try {
        const parsed = iface.parseLog(log);
        if (parsed && parsed.name === "BatchRecorded") {
          batchId = parsed.args.batchId.toString();
          break;
        }
      } catch {
        // log belongs to a different contract, skip
      }
    }

    console.log(`Batch confirmed on-chain. batchId=${batchId} tx=${tx.hash}`);

    for (const entry of batch) {
      pendingResults.set(entry.pendingId, {
        status: "confirmed",
        txHash: tx.hash,
        batchId,
      });
    }
  } catch (err) {
    console.error(`Batch flush failed: ${err.message}`);
    for (const entry of batch) {
      pendingResults.set(entry.pendingId, {
        status: "failed",
        error: err.message,
      });
    }
  }
}

setInterval(flushBatch, Number(BATCH_INTERVAL_MS));
console.log(`Batch interval: ${Number(BATCH_INTERVAL_MS) / 1000}s`);

// --- HTTP API -------------------------------------------------------------

const app = express();
app.use(express.json());

/**
 * POST /record
 *
 * Called by kds when a new message is sent. Queues the message for the next
 * batch flush. Returns a pendingId immediately — the tx hash is not yet known.
 *
 * Body:     { conversationId: number, msgId: number, ciphertext: string }
 * Response: { pendingId: string }
 */
app.post("/record", (req, res) => {
  const { conversationId, msgId, ciphertext } = req.body;

  if (conversationId === undefined || msgId === undefined || !ciphertext) {
    return res.status(400).json({ error: "conversationId, msgId and ciphertext are required" });
  }

  const leaf = ethers.keccak256(ethers.toUtf8Bytes(ciphertext));
  const pendingId = crypto.randomUUID();

  queue.push({ pendingId, conversationId, msgId, leaf });
  pendingResults.set(pendingId, { status: "pending" });

  console.log(`Queued message ${msgId} (conversation ${conversationId}). pendingId=${pendingId}`);
  res.json({ pendingId });
});

/**
 * GET /pending/:id
 *
 * Called by kds to check whether a queued message has been confirmed on-chain.
 *
 * Response: { status: "pending" }
 *        or { status: "confirmed", txHash: string, batchId: string }
 *        or { status: "failed", error: string }
 */
app.get("/pending/:id", (req, res) => {
  const result = pendingResults.get(req.params.id);
  if (!result) {
    return res.status(404).json({ error: "unknown pendingId" });
  }
  res.json(result);
});

/**
 * GET /health
 */
app.get("/health", (_req, res) => {
  res.json({ status: "ok", queueLength: queue.length });
});

app.listen(PORT, () => {
  console.log(`Blockchain sidecar running on port ${PORT}`);
  console.log(`Contract: ${CONTRACT_ADDRESS}`);
});
