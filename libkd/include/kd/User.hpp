#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace kd {

/**
 * @brief Represents a user in the Kingdom system.
 */
struct User {
  uint64_t id;
  std::string username;
  std::string displayName;
  std::string publicKey;  // Base64 encoded public key for E2EE

  NLOHMANN_DEFINE_TYPE_INTRUSIVE(User, id, username, displayName, publicKey)
};

}  // namespace kd
