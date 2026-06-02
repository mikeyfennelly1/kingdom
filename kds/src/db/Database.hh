#pragma once
#include <cstdint>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <kd/User.hpp>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace kd {

class Database {
 public:
  explicit Database(const std::string& connectionString);

  // Insert a new user, returns the new user's id.
  // Throws std::runtime_error if the username is already taken.
  uint64_t createUser(const std::string& username, const std::string& passwordHash,
                      const std::string& publicKey);

  // Fetch a user by username. Returns std::nullopt if not found.
  std::optional<User> getUserByUsername(const std::string& username);

  // Fetch all users (id + username only, no password hash).
  std::vector<User> getAllUsers();

  // Fetch a user's published public key. Returns std::nullopt if the user does not exist.
  std::optional<std::string> getUserPublicKey(uint64_t userId);

  // Returns true if a user exists.
  bool userExists(uint64_t userId);

  // Create a conversation and add participants. Returns conversation id.
  uint64_t createConversation(const std::string& name, const std::vector<uint64_t>& participantIds);

  // Get all conversations a user is a participant in.
  std::vector<kd::Conversation> getConversationsByUserId(uint64_t userId);

  // Insert a message. timestamp is Unix ms (passed in from server). Returns message id.
  uint64_t createMessage(uint64_t conversationId, uint64_t senderId, const std::string& payload,
                         uint64_t timestamp, std::optional<uint64_t> recipientId = std::nullopt,
                         std::optional<uint64_t> oneTimePreKeyId = std::nullopt);

  // Get all messages in a conversation ordered by timestamp ASC.
  std::vector<kd::Message> getMessagesByConversationId(uint64_t conversationId);

  // Get messages in a conversation that userId still has access to.
  std::vector<kd::Message> getMessagesByConversationIdForUser(uint64_t conversationId,
                                                              uint64_t userId);

  // Delete a message only when it belongs to the conversation and sender.
  bool deleteMessage(uint64_t conversationId, uint64_t messageId, uint64_t senderId);

  // Revoke targetUserId's future access to a message. Only the sender can revoke.
  bool revokeMessageAccess(uint64_t conversationId, uint64_t messageId, uint64_t senderId,
                           uint64_t targetUserId, uint64_t revokedAt);

  // Update the blockchain_digest field of a message after on-chain recording.
  void updateMessageBlockchainDigest(uint64_t msgId, const std::string& digest);

  // Return (msgId, pendingId) pairs for messages whose blockchain_digest starts with "pending:".
  std::vector<std::pair<uint64_t, std::string>> getPendingBlockchainMessages();

  // Returns true if userId is a participant in conversationId.
  bool isParticipant(uint64_t conversationId, uint64_t userId);

 private:
  void initSchema_();

  std::mutex mutex_;
  pqxx::connection conn_;
};

}  // namespace kd
