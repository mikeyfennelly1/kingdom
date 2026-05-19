#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

namespace kd {

/**
 * @brief REST API Server for the Kingdom system.
 */
class Server {
public:
    /**
     * @brief Construct a new Server object
     * 
     * @param host Host address to bind to
     * @param port Port number to listen on
     */
    Server(const std::string& host = "0.0.0.0", int port = 8080);

    /**
     * @brief Start the server and block until it stops.
     */
    void start();

private:
    std::string host_;
    int port_;
    httplib::Server svr_;

    /**
     * @brief Configure API routes
     */
    void setupRoutes();
};

} // namespace kd
