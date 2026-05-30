#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace kd {

/**
 * @brief Client for the Kingdom Server REST API.
 */
class Client {
 public:
  /**
   * @brief Construct a new Client object
   *
   * @param baseUrl    Base URL of the server (e.g., "https://localhost:8443")
   * @param caCertPath Path to the CA certificate for TLS verification. Required for
   *                   self-signed certs; leave empty to use system CAs.
   */
  Client(const std::string& baseUrl, std::string caCertPath = "");

  /**
   * @brief Check server health
   *
   * @return nlohmann::json Health status
   */
  nlohmann::json getHealth();

  /**
   * @brief Get server information
   *
   * @return nlohmann::json Server info
   */
  nlohmann::json getInfo();

  /**
   * @brief Get all conversations for a user
   *
   * @param userId User ID
   * @return nlohmann::json List of conversations
   */
  nlohmann::json getConversations(uint64_t userId);
  nlohmann::json getUsers();

  nlohmann::json signup(const std::string& username, const std::string& password);
  nlohmann::json login(const std::string& username, const std::string& password);
  nlohmann::json logout();
  void setAuthToken(const std::string& authToken);
  void clearAuthToken();
  void setSessionToken(const std::string& sessionToken);
  void clearSessionToken();
  std::string getPublicKey(uint64_t userId);

  nlohmann::json createConversation(const std::string& name,
                                    const std::vector<uint64_t>& participantIds);
  nlohmann::json sendMessage(uint64_t conversationId, uint64_t senderId,
                             const std::string& payload);
  nlohmann::json sendMessage(uint64_t conversationId, uint64_t senderId, uint64_t recipientId,
                             const std::string& payload);
  nlohmann::json getMessages(uint64_t conversationId);
  nlohmann::json deleteMessage(uint64_t conversationId, uint64_t messageId);
  nlohmann::json revokeMessageAccess(uint64_t conversationId, uint64_t messageId,
                                     uint64_t targetUserId);

 private:
  std::string baseUrl_;
  std::string caCertPath_;
  std::string authToken_;
};

}  // namespace kd
