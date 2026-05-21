#pragma once
#include <httplib.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include "../db/Database.hh"
#include "../security/SecurityFilterChain.hh"

namespace kd {

const int defaultPortNumber = 8080;
const std::string defaultListenHost = "0.0.0.0";

class Controller {
 public:
  Controller(std::string host, int port, std::string dbConnectionString, std::string sidecarUrl,
             std::string certPath, std::string keyPath);

  void start();

 private:
  std::string host_;
  int port_;
  std::string sidecarUrl_;
  httplib::SSLServer svr_;
  std::unique_ptr<SecurityFilterChain> securityFilterChain_;
  Database db_;
  std::unordered_map<std::string, uint64_t> sessions_;
  std::mutex sessionsMutex_;

  void setupRoutes();
  void healthController_();
  void authController_();
  void conversationController_();
  void messageController_();
  void basicApiInfo_();
  void notFoundHandler_();
  std::string createSession_(uint64_t userId);
  std::optional<uint64_t> authenticatedUserId_(const httplib::Request& req);
  void clearSession_(const httplib::Request& req);
};

auto configure() -> kd::Controller;

}  // namespace kd
