#pragma once
#include <httplib.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include "../security/SecurityFilterChain.hh"

namespace kd {

const int defaultPortNumber = 8080;
const std::string defaultListenHost = "0.0.0.0";

class Controller {
 public:
  Controller(std::string host = defaultListenHost, int port = defaultPortNumber);

  void start();

 private:
  std::string host_;
  int port_;
  httplib::Server svr_;
  std::unique_ptr<SecurityFilterChain> securityFilterChain_;

  void setupRoutes();
  void healthController_();
  void authController_();
  void conversationController_();
  void basicApiInfo_();
  void notFoundHandler_();
};

auto configure() -> kd::Controller;

}  // namespace kd
