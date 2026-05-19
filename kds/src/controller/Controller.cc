#include "Controller.hh"

#include <httplib.h>

#include <kd/Conversation.hpp>

namespace kd {

Controller::Controller(std::string host, int port) : host_(std::move(host)), port_(port) {
  std::vector<std::string> securityPredicates = {
      "ValidateSenderAuthenticity", "ValidateUntampered", "ValidateAuthenticated"};
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
  basicApiInfo_();

  // conversations
  conversationController_();
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
  svr_.Get(R"(/users/(\d+)/conversations)",
           [](const httplib::Request& req, httplib::Response& res) -> void {
             std::string userIdStr = req.matches[1];
             uint64_t userId = std::stoull(userIdStr);

             spdlog::info("Fetching conversations for user: {}", userId);

             // Mock data
             std::vector<kd::Conversation> convs = {
                 {1, "General Discussion", {userId, 2, 3}, 1716124800},
                 {2, "Security Team", {userId, 4}, 1716124900}};

             nlohmann::json result = convs;
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
