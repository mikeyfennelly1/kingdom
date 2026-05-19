#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace kd {

/**
 * @brief Client for the Kingdom Server REST API.
 */
class Client {
public:
    /**
     * @brief Construct a new Client object
     * 
     * @param baseUrl Base URL of the server (e.g., "http://localhost:8080")
     */
    Client(const std::string& baseUrl);

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

    nlohmann::json signup(const std::string& username, const std::string& password);
    nlohmann::json login(const std::string& username, const std::string& password);
    nlohmann::json logout();

private:
    std::string baseUrl_;
};

} // namespace kd
