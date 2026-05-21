#include "kd/Client.hpp"
#include <httplib.h>
#include <stdexcept>

namespace {

httplib::Client makeClient(const std::string& baseUrl, const std::string& caCertPath) {
    httplib::Client cli(baseUrl);
    if (!caCertPath.empty()) {
        cli.set_ca_cert_path(caCertPath);
        cli.enable_server_certificate_verification(true);
    }
    return cli;
}

}  // namespace

namespace kd {

Client::Client(const std::string& baseUrl, std::string caCertPath)
    : baseUrl_(baseUrl), caCertPath_(std::move(caCertPath)) {}

nlohmann::json Client::getHealth() {
    auto cli = makeClient(baseUrl_, caCertPath_);
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
    auto cli = makeClient(baseUrl_, caCertPath_);
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
    auto cli = makeClient(baseUrl_, caCertPath_);
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
    auto cli = makeClient(baseUrl_, caCertPath_);
    nlohmann::json body = {{"username", username}, {"password", password}};
    if (auto res = cli.Post("/signup", body.dump(), "application/json")) {
        if (res->status == 200 || res->status == 201) {
            auto response = nlohmann::json::parse(res->body);
            if (response.contains("sessionToken")) sessionToken_ = response["sessionToken"];
            return response;
        }
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

nlohmann::json Client::login(const std::string& username, const std::string& password) {
    auto cli = makeClient(baseUrl_, caCertPath_);
    nlohmann::json body = {{"username", username}, {"password", password}};
    if (auto res = cli.Post("/login", body.dump(), "application/json")) {
        if (res->status == 200) {
            auto response = nlohmann::json::parse(res->body);
            if (response.contains("sessionToken")) sessionToken_ = response["sessionToken"];
            return response;
        }
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

nlohmann::json Client::logout() {
    auto cli = makeClient(baseUrl_, caCertPath_);
    httplib::Headers headers;
    if (!sessionToken_.empty()) headers.emplace("X-KD-Session", sessionToken_);
    if (auto res = cli.Post("/logout", headers)) {
        if (res->status == 200) {
            clearSessionToken();
            return nlohmann::json::parse(res->body);
        }
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

void Client::setSessionToken(const std::string& sessionToken) {
    sessionToken_ = sessionToken;
}

void Client::clearSessionToken() {
    sessionToken_.clear();
}

nlohmann::json Client::createConversation(const std::string& name,
                                          const std::vector<uint64_t>& participantIds) {
    auto cli = makeClient(baseUrl_, caCertPath_);
    nlohmann::json body = {{"name", name}, {"participantIds", participantIds}};
    if (auto res = cli.Post("/conversations", body.dump(), "application/json")) {
        if (res->status == 200 || res->status == 201) return nlohmann::json::parse(res->body);
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

nlohmann::json Client::sendMessage(uint64_t conversationId, uint64_t senderId,
                                   const std::string& payload) {
    auto cli = makeClient(baseUrl_, caCertPath_);
    cli.set_read_timeout(120);
    std::string path = "/conversations/" + std::to_string(conversationId) + "/messages";
    nlohmann::json body = {{"senderId", senderId}, {"payload", payload}};
    httplib::Headers headers;
    if (!sessionToken_.empty()) headers.emplace("X-KD-Session", sessionToken_);
    if (auto res = cli.Post(path, headers, body.dump(), "application/json")) {
        if (res->status == 200 || res->status == 201) return nlohmann::json::parse(res->body);
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

nlohmann::json Client::getMessages(uint64_t conversationId) {
    auto cli = makeClient(baseUrl_, caCertPath_);
    std::string path = "/conversations/" + std::to_string(conversationId) + "/messages";
    if (auto res = cli.Get(path)) {
        if (res->status == 200) return nlohmann::json::parse(res->body);
        throw std::runtime_error("Server returned status " + std::to_string(res->status));
    }
    throw std::runtime_error("Failed to connect to server at " + baseUrl_);
}

} // namespace kd
