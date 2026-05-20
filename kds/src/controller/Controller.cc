#include "Controller.hh"

#include <httplib.h>

#include <kd/Conversation.hpp>

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
    spdlog::debug("Controller: Pre-routing handler for {} {}", req.method, req.path);
    auto error = securityFilterChain_->Execute(req);
    if (error.has_value()) {
      spdlog::debug("Controller: Security check failed for {} {}: {}", req.method, req.path,
                    error->message);
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
}

void Controller::authController_() {
  svr_.Post("/signup", [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Sign-up request received");
    spdlog::debug("Controller: Processing /signup");

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
      spdlog::debug("Controller: /signup failed - invalid body");
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "username and password required"}}.dump(),
                      "application/json");
      return;
    }

    std::string username = body["username"];
    std::string password = body["password"];

    try {
      uint64_t id = db_.createUser(username, password);
      spdlog::info("Created user '{}' with id {}", username, id);
      res.status = 201;
      res.set_content(nlohmann::json{{"id", id}, {"username", username}}.dump(),
                      "application/json");
      spdlog::debug("Controller: /signup successful for user '{}'", username);
    } catch (const std::runtime_error& e) {
      spdlog::debug("Controller: /signup failed for user '{}': {}", username, e.what());
      res.status = 409;
      res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
    }
  });

  // Login endpoint
  svr_.Post("/login", [](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Login request received: {}", req.body);
    spdlog::debug("Controller: Processing /login");
    nlohmann::json response = {{"status", "success"}, {"message", "User logged in (stub)"}};
    res.set_content(response.dump(), "application/json");
  });

  // Logout endpoint
  svr_.Post("/logout", [](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Logout request received: {}", req.body);
    spdlog::debug("Controller: Processing /logout");
    nlohmann::json response = {{"status", "success"}, {"message", "User logged out (stub)"}};
    res.set_content(response.dump(), "application/json");
  });
}

void Controller::start() {
  spdlog::info("Starting Kingdom Server on {}:{}", host_, port_);
  spdlog::debug("Controller: Server listening start...");

  if (!svr_.listen(host_, port_)) {
    spdlog::error("Failed to start server on {}:{}", host_, port_);
    return;
  }
}

void Controller::healthController_() {
  svr_.Get("/health", [](const httplib::Request&, httplib::Response& res) -> void {
    spdlog::debug("Controller: Processing /health");
    nlohmann::json status = {{"status", "ok"}};
    res.set_content(status.dump(), "application/json");
  });
}

void Controller::conversationController_() {
  svr_.Get(R"(/users/(\d+)/conversations)",
           [](const httplib::Request& req, httplib::Response& res) -> void {
             std::string userIdStr = req.matches[1];
             uint64_t userId = std::stoull(userIdStr);

             spdlog::info("Fetching conversations for user: {}", userId);
             spdlog::debug("Controller: Processing /users/{}/conversations", userId);

             // Mock data
             std::vector<kd::Conversation> convs = {
                 {1, "General Discussion", {userId, 2, 3}, 1716124800},
                 {2, "Security Team", {userId, 4}, 1716124900}};

             nlohmann::json result = convs;
             res.set_content(result.dump(), "application/json");
             spdlog::debug("Controller: Returned {} conversations for user {}", convs.size(),
                           userId);
           });
}

void Controller::basicApiInfo_() {
  svr_.Get("/", [](const httplib::Request&, httplib::Response& res) -> void {
    spdlog::debug("Controller: Processing / (basic info)");
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
