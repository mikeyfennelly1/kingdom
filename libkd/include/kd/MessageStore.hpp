#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Message.hpp"

namespace kd {

/**
 * @brief In-memory store for messages fetched during the current session.
 * Also maintains a public-key cache keyed by userId to avoid redundant server lookups.
 */
class MessageStore {
 public:
  void add(Message message);

  [[nodiscard]] const std::vector<Message>& getAll() const;

  [[nodiscard]] std::vector<Message> findBySender(uint64_t senderId) const;

  void clear();

  // Public-key cache keyed by userId.
  void cachePublicKey(uint64_t userId, const std::string& publicKey);
  [[nodiscard]] std::optional<std::string> getCachedPublicKey(uint64_t userId) const;

 private:
  std::vector<Message> messages_;
  std::map<uint64_t, std::string> publicKeyCache_;
};

}  // namespace kd
