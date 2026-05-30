#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <kd/Message.hpp>
#include <nlohmann/json_fwd.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kd {

class MessageStore {
 public:
  static constexpr size_t kEncryptionKeySize = 32;
  static constexpr size_t kSaltSize = 16;

  MessageStore() = default;
  explicit MessageStore(const std::string& username);
  explicit MessageStore(std::filesystem::path storePath);

  static MessageStore encryptedForUser(const std::string& username, const std::string& password);
  static MessageStore encryptedAtPath(std::filesystem::path storePath, const std::string& username,
                                      const std::string& password);

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
  MessageStore(std::filesystem::path storePath, std::string username,
               std::array<unsigned char, kEncryptionKeySize> encryptionKey,
               std::array<unsigned char, kSaltSize> salt, unsigned long long opsLimit,
               size_t memLimit);

  [[nodiscard]] nlohmann::json readStore_() const;
  void writeStore_(const nlohmann::json& store) const;

  std::vector<Message> messages_;
  std::map<uint64_t, std::string> publicKeyCache_;
  std::filesystem::path storePath_;
  std::string username_;
  std::array<unsigned char, kEncryptionKeySize> encryptionKey_{};
  std::array<unsigned char, kSaltSize> salt_{};
  unsigned long long opsLimit_ = 0;
  size_t memLimit_ = 0;
  bool encryptedAtRest_ = false;
};

}  // namespace kd
