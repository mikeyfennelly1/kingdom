#include "kd/Client.hpp"
#include <httplib.h>
#include <stdexcept>

namespace kd {

Client::Client(const std::string& baseUrl) : baseUrl_(baseUrl) {}

nlohmann::json Client::getHealth() {
    httplib::Client cli(baseUrl_);
    if (auto res = cli.Get("/health")) {
        if (res->status == 200) {
            return nlohmann::json::parse(res->body);
        }
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    } else {
        throw std::runtime_error("Failed to connect to server at " + baseUrl_);
    }
}

nlohmann::json Client::getInfo() {
    httplib::Client cli(baseUrl_);
    if (auto res = cli.Get("/")) {
        if (res->status == 200) {
            return nlohmann::json::parse(res->body);
        }
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    } else {
        throw std::runtime_error("Failed to connect to server at " + baseUrl_);
    }
}

nlohmann::json Client::getConversations(uint64_t userId) {
    httplib::Client cli(baseUrl_);
    std::string path = "/users/" + std::to_string(userId) + "/conversations";
    if (auto res = cli.Get(path.c_str())) {
        if (res->status == 200) {
            return nlohmann::json::parse(res->body);
        }
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    } else {
        throw std::runtime_error("Failed to connect to server at " + baseUrl_);
    }
}

} // namespace kd
