#include "Server.hh"
#include <kd/Conversation.hpp>

namespace kd {

Server::Server(const std::string& host, int port) 
    : host_(host), port_(port) {
    setupRoutes();
}

void Server::setupRoutes() {
    // Health check endpoint
    svr_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json status = {{"status", "ok"}};
        res.set_content(status.dump(), "application/json");
    });

    // Basic API info
    svr_.Get("/", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json info = {
            {"name", "Kingdom Server"},
            {"version", "1.0"}
        };
        res.set_content(info.dump(), "application/json");
    });

    // Get all conversations for a user
    svr_.Get(R"(/users/(\d+)/conversations)", [](const httplib::Request& req, httplib::Response& res) {
        std::string userIdStr = req.matches[1];
        uint64_t userId = std::stoull(userIdStr);
        
        spdlog::info("Fetching conversations for user: {}", userId);

        // Mock data
        std::vector<kd::Conversation> convs = {
            {1, "General Discussion", {userId, 2, 3}, 1716124800},
            {2, "Security Team", {userId, 4}, 1716124900}
        };

        nlohmann::json result = convs;
        res.set_content(result.dump(), "application/json");
    });

    // 404 handler
    svr_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404) {
            nlohmann::json error = {
                {"error", "Not Found"},
                {"status", 404}
            };
            res.set_content(error.dump(), "application/json");
        }
    });
}

void Server::start() {
    spdlog::info("Starting Kingdom Server on {}:{}", host_, port_);
    
    if (!svr_.listen(host_.c_str(), port_)) {
        spdlog::error("Failed to start server on {}:{}", host_, port_);
        return;
    }
}

} // namespace kd
