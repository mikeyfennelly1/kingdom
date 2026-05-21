#include "Controller.hh"

#include <httplib.h>

#include <chrono>
#include <iomanip>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <random>
#include <sstream>

namespace kd {

Controller::Controller(std::string host, int port, std::string dbConnectionString)
    : host_(std::move(host)), port_(port), db_(dbConnectionString) {
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
  std::random_device rd;
  std::mt19937_64 generator(rd());
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream token;
  token << std::hex << std::setfill('0') << std::setw(16) << dist(generator) << std::setw(16)
        << dist(generator);

  auto value = token.str();
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
      uint64_t id = db_.createUser(username, password);
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
    if (!user.has_value() || user->passwordHash != password) {
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

    kd::Message msg{msgId, senderId, convId, payload, "", now, ""};
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
