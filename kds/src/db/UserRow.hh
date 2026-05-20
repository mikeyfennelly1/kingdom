#pragma once
#include <cstdint>
#include <string>

namespace kd {

// Represents a row from the users table
struct UserRow {
  uint64_t id;
  std::string username;
  std::string passwordHash;
  std::string publicKey;
};

}  // namespace kd
