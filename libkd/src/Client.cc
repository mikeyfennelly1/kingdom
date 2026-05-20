#include "kd/Client.hpp"

#include <httplib.h>

#include <stdexcept>

namespace kd {

Client::Client(const std::string& baseUrl) : baseUrl_(baseUrl) {
}

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

nlohmann::json Client::signup(const std::string& username, const std::string& password) {
    httplib::Client cli(baseUrl_);
    nlohmann::json body = {{"username", username}, {"password", password}};
    if (auto res = cli.Post("/signup", body.dump(), "application/json")) {
        if (res->status == 200 || res->status == 201)
            return nlohmann::json::parse(res->body);
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

nlohmann::json Client::login(const std::string& username, const std::string& password) {
    httplib::Client cli(baseUrl_);
    nlohmann::json body = {{"username", username}, {"password", password}};
    if (auto res = cli.Post("/login", body.dump(), "application/json")) {
        if (res->status == 200)
            return nlohmann::json::parse(res->body);
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

nlohmann::json Client::logout() {
    httplib::Client cli(baseUrl_);
    if (auto res = cli.Post("/logout")) {
        if (res->status == 200)
            return nlohmann::json::parse(res->body);
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

}  // namespace kd
