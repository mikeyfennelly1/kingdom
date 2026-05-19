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

private:
    std::string baseUrl_;
};

} // namespace kd
