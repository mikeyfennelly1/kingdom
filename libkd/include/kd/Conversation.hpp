#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace kd {

/**
 * @brief Represents a messaging thread between multiple users.
 */
struct Conversation {
  uint64_t id;
  std::string name;
  std::vector<uint64_t> participantIds;
  uint64_t createdAt;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE(Conversation, id, name, participantIds, createdAt)
};

}  // namespace kd
