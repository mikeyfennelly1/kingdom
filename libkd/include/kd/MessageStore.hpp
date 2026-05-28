#pragma once

#include <cstdint>
#include <filesystem>
#include <kd/Message.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kd {

class MessageStore {
 public:
  MessageStore() = default;
  explicit MessageStore(const std::string& username);
  explicit MessageStore(std::filesystem::path storePath);

  void add(Message message);
  [[nodiscard]] const std::vector<Message>& getAll() const;
  [[nodiscard]] std::vector<Message> findBySender(uint64_t senderId) const;
  void clear();

  void cachePublicKey(uint64_t userId, const std::string& publicKey);
  [[nodiscard]] std::optional<std::string> getCachedPublicKey(uint64_t userId) const;

  [[nodiscard]] std::optional<std::string> getPlaintext(uint64_t messageId) const;
  void savePlaintext(uint64_t messageId, uint64_t conversationId, uint64_t senderId,
                     uint64_t timestamp, const std::string& plaintext) const;
  void deletePlaintext(uint64_t messageId) const;

 private:
  std::vector<Message> messages_;
  std::map<uint64_t, std::string> publicKeyCache_;
  std::filesystem::path storePath_;
};

}  // namespace kd
