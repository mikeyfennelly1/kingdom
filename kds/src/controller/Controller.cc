#include "Controller.hh"

#include <httplib.h>
#include <sodium.h>

#include <chrono>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <sstream>

namespace kd {

namespace {

std::string hashPassword(const std::string& password) {
  char encodedHash[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str_alg(encodedHash, password.c_str(), password.size(),
                            crypto_pwhash_OPSLIMIT_INTERACTIVE,
                            crypto_pwhash_MEMLIMIT_INTERACTIVE,
                            crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to hash password");
  }
  return encodedHash;
}

bool verifyPassword(const std::string& encodedHash, const std::string& password) {
  return crypto_pwhash_str_verify(encodedHash.c_str(), password.c_str(), password.size()) == 0;
}

}  // namespace

Controller::Controller(std::string host, int port, std::string dbConnectionString,
                       std::string sidecarUrl, std::string certPath, std::string keyPath)
    : host_(std::move(host)), port_(port), sidecarUrl_(std::move(sidecarUrl)),
      svr_(certPath.c_str(), keyPath.c_str()), db_(dbConnectionString) {
  if (sodium_init() < 0) {
    throw std::runtime_error("Failed to initialize libsodium");
  }
  std::vector<std::string> securityPredicates = {"ValidateSenderAuthenticity", "ValidateUntampered",
                                                 "ValidateAuthenticated"};
  securityFilterChain_ = std::make_unique<SecurityFilterChain>(securityPredicates);

  setupRoutes();
}

void Controller::setupRoutes() {
  svr_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    auto error = securityFilterChain_->Execute(req);
    if (error.has_value()) {
      nlohmann::json errorJson = {{"error", error->message}};
      res.status = error->httpStatusCode;
      res.set_content(errorJson.dump(), "application/json");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  notFoundHandler_();
  healthController_();
  authController_();
  basicApiInfo_();

  // conversations
  conversationController_();
  messageController_();
}

std::string Controller::createSession_(uint64_t userId) {
  unsigned char buf[32];
  randombytes_buf(buf, sizeof(buf));
  char hex[65];
  sodium_bin2hex(hex, sizeof(hex), buf, sizeof(buf));

  std::string value(hex);
  std::lock_guard<std::mutex> lock(sessionsMutex_);
  sessions_[value] = userId;
  return value;
}

std::optional<uint64_t> Controller::authenticatedUserId_(const httplib::Request& req) {
  auto tokenHeader = req.get_header_value("X-KD-Session");
  if (tokenHeader.empty()) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(sessionsMutex_);
  auto session = sessions_.find(tokenHeader);
  if (session == sessions_.end()) {
    return std::nullopt;
  }
  return session->second;
}

void Controller::clearSession_(const httplib::Request& req) {
  auto tokenHeader = req.get_header_value("X-KD-Session");
  if (tokenHeader.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(sessionsMutex_);
  sessions_.erase(tokenHeader);
}

void Controller::authController_() {
  svr_.Post("/signup", [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Sign-up request received");

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "username and password required"}}.dump(),
                      "application/json");
      return;
    }

    std::string username = body["username"];
    std::string password = body["password"];

    try {
      auto passwordHash = hashPassword(password);
      uint64_t id = db_.createUser(username, passwordHash);
      auto sessionToken = createSession_(id);
      spdlog::info("Created user '{}' with id {}", username, id);
      res.status = 201;
      res.set_content(
          nlohmann::json{{"id", id}, {"username", username}, {"sessionToken", sessionToken}}.dump(),
          "application/json");
    } catch (const std::runtime_error& e) {
      res.status = 409;
      res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
    }
  });

  // Login endpoint
  svr_.Post("/login", [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Login request received: {}", req.body);
    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "username and password required"}}.dump(),
                      "application/json");
      return;
    }

    std::string username = body["username"];
    std::string password = body["password"];
    auto user = db_.getUserByUsername(username);
    if (!user.has_value() || !verifyPassword(user->passwordHash, password)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"error", "invalid username or password"}}.dump(),
                      "application/json");
      return;
    }

    auto sessionToken = createSession_(user->id);
    res.set_content(nlohmann::json{{"id", user->id},
                                   {"username", user->username},
                                   {"sessionToken", sessionToken}}
                        .dump(),
                    "application/json");
  });

  // Logout endpoint
  svr_.Post("/logout", [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Logout request received: {}", req.body);
    clearSession_(req);
    nlohmann::json response = {{"status", "success"}, {"message", "User logged out (stub)"}};
    res.set_content(response.dump(), "application/json");
  });
}

void Controller::start() {
  spdlog::info("Starting Kingdom Server on {}:{}", host_, port_);

  if (!svr_.listen(host_, port_)) {
    spdlog::error("Failed to start server on {}:{}", host_, port_);
    return;
  }
}

void Controller::healthController_() {
  svr_.Get("/health", [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json status = {{"status", "ok"}};
    res.set_content(status.dump(), "application/json");
  });
}

void Controller::conversationController_() {
  svr_.Post("/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("name") || !body.contains("participantIds")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "name and participantIds required"}}.dump(),
                      "application/json");
      return;
    }

    std::string name = body["name"];
    auto participantIds = body["participantIds"].get<std::vector<uint64_t>>();

    uint64_t id = db_.createConversation(name, participantIds);
    spdlog::info("Created conversation '{}' with id {}", name, id);
    res.status = 201;
    res.set_content(nlohmann::json{{"id", id}, {"name", name}}.dump(), "application/json");
  });

  svr_.Get(R"(/users/(\d+)/conversations)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             uint64_t userId = std::stoull(std::string(req.matches[1]));
             spdlog::info("Fetching conversations for user: {}", userId);
             auto convs = db_.getConversationsByUserId(userId);
             nlohmann::json result = convs;
             res.set_content(result.dump(), "application/json");
           });
}

void Controller::messageController_() {
  svr_.Post(R"(/conversations/(\d+)/messages)", [this](const httplib::Request& req,
                                                       httplib::Response& res) {
    uint64_t convId = std::stoull(std::string(req.matches[1]));

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("senderId") || !body.contains("payload")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "senderId and payload required"}}.dump(),
                      "application/json");
      return;
    }

    uint64_t senderId = body["senderId"];
    std::string payload = body["payload"];
    auto authenticatedUserId = authenticatedUserId_(req);
    if (!authenticatedUserId.has_value()) {
      res.status = 401;
      res.set_content(nlohmann::json{{"error", "login required"}}.dump(), "application/json");
      return;
    }
    if (*authenticatedUserId != senderId) {
      res.status = 403;
      res.set_content(nlohmann::json{{"error", "session user cannot send as another user"}}.dump(),
                      "application/json");
      return;
    }

    auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());

    uint64_t msgId = db_.createMessage(convId, senderId, payload, now);
    spdlog::info("Created message {} in conversation {}", msgId, convId);

    // Record hash on-chain via the blockchain sidecar
    std::string txHash;
    try {
        httplib::Client sidecar(sidecarUrl_);
        sidecar.set_connection_timeout(30);
        sidecar.set_read_timeout(60);
        nlohmann::json sidecarBody = {{"conversationId", convId}, {"ciphertext", payload}};
        auto sidecarRes = sidecar.Post("/record", sidecarBody.dump(), "application/json");
        if (sidecarRes && sidecarRes->status == 200) {
            auto sidecarJson = nlohmann::json::parse(sidecarRes->body);
            txHash = sidecarJson["txHash"].get<std::string>();
            db_.updateMessageBlockchainDigest(msgId, txHash);
            spdlog::info("Blockchain digest recorded for message {}: {}", msgId, txHash);
        } else {
            spdlog::warn("Blockchain sidecar unavailable for message {}", msgId);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Blockchain sidecar error for message {}: {}", msgId, e.what());
    }

    kd::Message msg{msgId, senderId, convId, payload, "", now, txHash};
    nlohmann::json result = msg;
    res.status = 201;
    res.set_content(result.dump(), "application/json");
  });

  svr_.Get(R"(/conversations/(\d+)/messages)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             uint64_t convId = std::stoull(std::string(req.matches[1]));
             auto msgs = db_.getMessagesByConversationId(convId);
             nlohmann::json result = msgs;
             res.set_content(result.dump(), "application/json");
           });
}

void Controller::basicApiInfo_() {
  svr_.Get("/", [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json info = {{"name", "Kingdom Server"}, {"version", "1.0"}};
    res.set_content(info.dump(), "application/json");
  });
}

void Controller::notFoundHandler_() {
  svr_.set_error_handler([](const httplib::Request&, httplib::Response& res) -> void {
    if (res.status == httplib::NotFound_404) {
      nlohmann::json error = {{"error", "Not Found"}, {"status", httplib::NotFound_404}};
      res.set_content(error.dump(), "application/json");
    }
  });
}

}  // namespace kd
