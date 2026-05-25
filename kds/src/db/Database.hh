#pragma once
#include <cstdint>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

#include "UserRow.hh"

namespace kd {

class Database {
 public:
  explicit Database(const std::string& connectionString);

  // Insert a new user, returns the new user's id.
  // Throws std::runtime_error if the username is already taken.
  uint64_t createUser(const std::string& username, const std::string& passwordHash,
                      const std::string& publicKey);

  // Fetch a user by username. Returns std::nullopt if not found.
  std::optional<UserRow> getUserByUsername(const std::string& username);

  // Fetch a user's published public key. Returns std::nullopt if the user does not exist.
  std::optional<std::string> getUserPublicKey(uint64_t userId);

  // Create a conversation and add participants. Returns conversation id.
  uint64_t createConversation(const std::string& name, const std::vector<uint64_t>& participantIds);

  // Get all conversations a user is a participant in.
  std::vector<kd::Conversation> getConversationsByUserId(uint64_t userId);

  // Insert a message. timestamp is Unix ms (passed in from server). Returns message id.
  uint64_t createMessage(uint64_t conversationId, uint64_t senderId, const std::string& payload,
                         uint64_t timestamp);

  // Get all messages in a conversation ordered by timestamp ASC.
  std::vector<kd::Message> getMessagesByConversationId(uint64_t conversationId);

  // Update the blockchain_digest field of a message after on-chain recording.
  void updateMessageBlockchainDigest(uint64_t msgId, const std::string& digest);

  // Returns true if userId is a participant in conversationId.
  bool isParticipant(uint64_t conversationId, uint64_t userId);

 private:
  void initSchema_();

  pqxx::connection conn_;
};

}  // namespace kd
