#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace kd {

/**
 * @brief Represents a messaging thread between multiple users.
 */
struct Conversation {
    uint64_t id;
    std::string name;
    std::vector<uint64_t> participantIds;
    uint64_t createdAt;
};

} // namespace kd
