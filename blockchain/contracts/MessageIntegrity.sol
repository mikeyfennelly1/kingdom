// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// @title MessageIntegrity
/// @notice Records keccak256 hashes of message conversation payloads on-chain
///         to provide tamper-evident integrity verification.
contract MessageIntegrity {
    address public owner;

    /// @notice Emitted when a conversation hash is recorded
    /// @param conversationId The ID of the conversation
    /// @param hash           keccak256 hash of the conversation payload
    /// @param timestamp      Block timestamp at time of recording
    event HashRecorded(
        uint256 indexed conversationId,
        bytes32 hash,
        uint256 timestamp
    );

    error Unauthorized();

    mapping(uint256 => bytes32) public hashes;
    mapping(uint256 => uint256) public timestamps;

    constructor() {
        owner = msg.sender;
    }

    modifier onlyOwner() {
        if (msg.sender != owner) revert Unauthorized();
        _;
    }

    /// @notice Record a conversation payload hash on-chain
    /// @param conversationId The ID of the conversation being recorded
    /// @param hash           keccak256 hash of the ciphertext payload
    function recordHash(uint256 conversationId, bytes32 hash) external onlyOwner {
        hashes[conversationId] = hash;
        timestamps[conversationId] = block.timestamp;
        emit HashRecorded(conversationId, hash, block.timestamp);
    }
}
