#pragma once
#include <string>
#include <cstdint>

namespace kd {

/**
 * @brief Represents a single message within a conversation.
 */
struct Message {
    uint64_t id;
    uint64_t senderId;
    uint64_t conversationId;
    
    std::string payload;     // Encrypted message content
    std::string signature;   // Digital signature for authenticity
    
    uint64_t timestamp;      // Unix timestamp (milliseconds)
    
    std::string blockchainDigest; // Hash stored on-chain for integrity
};

} // namespace kd
