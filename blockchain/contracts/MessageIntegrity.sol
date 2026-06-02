// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// @title MessageIntegrity
/// @notice Records Merkle roots of batched message ciphertext hashes on-chain
///         to provide tamper-evident integrity verification.
contract MessageIntegrity {
    address public owner;

    struct Batch {
        bytes32 root;
        uint256 timestamp;
        uint256 messageCount;
    }

    /// @notice Emitted when a batch of message hashes is recorded.
    /// @param batchId       Auto-incrementing batch identifier
    /// @param root          Merkle root of all leaf hashes in this batch
    /// @param timestamp     Block timestamp at time of recording
    /// @param messageCount  Number of messages in the batch
    /// @param leaves        Individual keccak256 leaf hashes (one per message)
    event BatchRecorded(
        uint256 indexed batchId,
        bytes32 root,
        uint256 timestamp,
        uint256 messageCount,
        bytes32[] leaves
    );

    error Unauthorized();
    error EmptyBatch();

    mapping(uint256 => Batch) public batches;
    uint256 public nextBatchId;

    constructor() {
        owner = msg.sender;
    }

    modifier onlyOwner() {
        if (msg.sender != owner) revert Unauthorized();
        _;
    }

    /// @notice Record a Merkle root for a batch of message ciphertext hashes.
    /// @param root   Merkle root computed from the leaf hashes off-chain
    /// @param leaves Individual keccak256(ciphertext) hashes — emitted for
    ///               independent verification without a server
    /// @return batchId The ID assigned to this batch
    function recordBatch(bytes32 root, bytes32[] calldata leaves)
        external
        onlyOwner
        returns (uint256)
    {
        if (leaves.length == 0) revert EmptyBatch();

        uint256 batchId = nextBatchId++;
        batches[batchId] = Batch(root, block.timestamp, leaves.length);
        emit BatchRecorded(batchId, root, block.timestamp, leaves.length, leaves);
        return batchId;
    }
}
