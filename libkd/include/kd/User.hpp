#pragma once
#include <string>
#include <cstdint>

namespace kd {

/**
 * @brief Represents a user in the Kingdom system.
 */
struct User {
    uint64_t id;
    std::string username;
    std::string displayName;
    std::string publicKey; // Base64 encoded public key for E2EE
};

} // namespace kd
