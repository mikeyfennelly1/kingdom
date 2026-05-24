#pragma once
#include <httplib.h>
#include <spdlog/spdlog.h>

#include <chrono>
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
             std::string certPath, std::string keyPath, std::string jwtSecret,
             uint64_t jwtTtlSeconds);

  void start();

 private:
  std::string host_;
  int port_;
  std::string sidecarUrl_;
  httplib::SSLServer svr_;
  std::unique_ptr<SecurityFilterChain> securityFilterChain_;
  Database db_;
  std::string jwtSecret_;
  uint64_t jwtTtlSeconds_;

  void setupRoutes();
  void healthController_();
  void authController_();
  void publicKeyController_();
  void conversationController_();
  void messageController_();
  void basicApiInfo_();
  void notFoundHandler_();
  std::string createSession_(uint64_t userId, const std::string& username) const;
  std::optional<uint64_t> authenticatedUserId_(const httplib::Request& req);
  bool isRateLimited_(const std::string& ip);

  struct RateLimitEntry {
    int count;
    std::chrono::steady_clock::time_point windowStart;
  };
  std::unordered_map<std::string, RateLimitEntry> rateLimitMap_;
  std::mutex rateLimitMutex_;
};

auto configure() -> kd::Controller;

}  // namespace kd
