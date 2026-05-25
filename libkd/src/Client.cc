#include "kd/Client.hpp"

#include <httplib.h>

#include <filesystem>
#include <stdexcept>
#include <system_error>

#include "kd/LocalKeyStore.hpp"

namespace {

httplib::Client makeClient(const std::string& baseUrl, const std::string& caCertPath) {
  httplib::Client cli(baseUrl);
  cli.enable_server_certificate_verification(true);
  if (!caCertPath.empty()) {
    cli.set_ca_cert_path(caCertPath);
  }
  return cli;
}

std::string connectError(const std::string& baseUrl, const std::string& caCertPath) {
  if (caCertPath.empty() && baseUrl.rfind("https://", 0) == 0) {
    return "TLS certificate verification failed — no CA certificate provided. Use --ca-cert to specify one.";
  }
  return "Failed to connect to server at " + baseUrl;
}

httplib::Headers authHeaders(const std::string& authToken) {
  httplib::Headers headers;
  if (!authToken.empty())
    headers.emplace("Authorization", "Bearer " + authToken);
  return headers;
}

}  // namespace

namespace kd {

Client::Client(const std::string& baseUrl, std::string caCertPath)
    : baseUrl_(baseUrl), caCertPath_(std::move(caCertPath)) {
}

nlohmann::json Client::getHealth() {
  auto cli = makeClient(baseUrl_, caCertPath_);
  if (auto res = cli.Get("/health")) {
    if (res->status == 200) {
      return nlohmann::json::parse(res->body);
    }
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  } else {
    throw std::runtime_error(connectError(baseUrl_, caCertPath_));
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
    throw std::runtime_error(connectError(baseUrl_, caCertPath_));
  }
}

nlohmann::json Client::getConversations(uint64_t userId) {
  auto cli = makeClient(baseUrl_, caCertPath_);
  std::string path = "/users/" + std::to_string(userId) + "/conversations";
  auto headers = authHeaders(authToken_);
  if (auto res = cli.Get(path, headers)) {
    if (res->status == 200) {
      return nlohmann::json::parse(res->body);
    }
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  } else {
    throw std::runtime_error(connectError(baseUrl_, caCertPath_));
  }
}

nlohmann::json Client::signup(const std::string& username, const std::string& password) {
  auto keyMaterial = LocalKeyStore::createForSignup(username, password);
  auto cleanupLocalKey = [&keyMaterial]() {
    std::error_code ignored;
    std::filesystem::remove(std::filesystem::path(keyMaterial.keyFilePath), ignored);
  };

  auto cli = makeClient(baseUrl_, caCertPath_);
  nlohmann::json body = {{"username", username},
                         {"password", password},
                         {"publicKey", keyMaterial.publicKey}};
  if (auto res = cli.Post("/signup", body.dump(), "application/json")) {
    if (res->status == 200 || res->status == 201) {
      auto response = nlohmann::json::parse(res->body);
      if (response.contains("token")) {
        authToken_ = response["token"];
      } else if (response.contains("sessionToken")) {
        authToken_ = response["sessionToken"];
      }
      response["localKeyFile"] = keyMaterial.keyFilePath;
      return response;
    }
    cleanupLocalKey();
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  cleanupLocalKey();
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

nlohmann::json Client::login(const std::string& username, const std::string& password) {
  auto cli = makeClient(baseUrl_, caCertPath_);
  nlohmann::json body = {{"username", username}, {"password", password}};
  if (auto res = cli.Post("/login", body.dump(), "application/json")) {
    if (res->status == 200) {
      auto response = nlohmann::json::parse(res->body);
      if (response.contains("token")) {
        authToken_ = response["token"];
      } else if (response.contains("sessionToken")) {
        authToken_ = response["sessionToken"];
      }
      return response;
    }
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

nlohmann::json Client::logout() {
  auto cli = makeClient(baseUrl_, caCertPath_);
  auto headers = authHeaders(authToken_);
  if (auto res = cli.Post("/logout", headers)) {
    if (res->status == 200) {
      clearAuthToken();
      return nlohmann::json::parse(res->body);
    }
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

void Client::setAuthToken(const std::string& authToken) {
  authToken_ = authToken;
}

void Client::clearAuthToken() {
  authToken_.clear();
}

void Client::setSessionToken(const std::string& sessionToken) {
  setAuthToken(sessionToken);
}

void Client::clearSessionToken() {
  clearAuthToken();
}

std::string Client::getPublicKey(uint64_t userId) {
  auto cli = makeClient(baseUrl_, caCertPath_);
  std::string path = "/users/" + std::to_string(userId) + "/public-key";
  if (auto res = cli.Get(path)) {
    if (res->status == 200) {
      auto body = nlohmann::json::parse(res->body);
      return body["publicKey"].get<std::string>();
    }
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

nlohmann::json Client::createConversation(const std::string& name,
                                          const std::vector<uint64_t>& participantIds) {
  auto cli = makeClient(baseUrl_, caCertPath_);
  nlohmann::json body = {{"name", name}, {"participantIds", participantIds}};
  auto headers = authHeaders(authToken_);
  if (auto res = cli.Post("/conversations", headers, body.dump(), "application/json")) {
    if (res->status == 200 || res->status == 201)
      return nlohmann::json::parse(res->body);
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

nlohmann::json Client::sendMessage(uint64_t conversationId, uint64_t senderId,
                                   const std::string& payload) {
  auto cli = makeClient(baseUrl_, caCertPath_);
  cli.set_read_timeout(120);
  std::string path = "/conversations/" + std::to_string(conversationId) + "/messages";
  nlohmann::json body = {{"senderId", senderId}, {"payload", payload}};
  auto headers = authHeaders(authToken_);
  if (auto res = cli.Post(path, headers, body.dump(), "application/json")) {
    if (res->status == 200 || res->status == 201)
      return nlohmann::json::parse(res->body);
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

nlohmann::json Client::getMessages(uint64_t conversationId) {
  auto cli = makeClient(baseUrl_, caCertPath_);
  std::string path = "/conversations/" + std::to_string(conversationId) + "/messages";
  auto headers = authHeaders(authToken_);
  if (auto res = cli.Get(path, headers)) {
    if (res->status == 200)
      return nlohmann::json::parse(res->body);
    throw std::runtime_error("Server returned status " + std::to_string(res->status));
  }
  throw std::runtime_error(connectError(baseUrl_, caCertPath_));
}

}  // namespace kd
