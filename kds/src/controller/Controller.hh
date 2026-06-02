#pragma once
#include <httplib.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "../db/Database.hh"
#include "../security/SecurityFilterChain.hh"

namespace kd {

const int defaultPortNumber = 8080;
const std::string defaultListenHost = "0.0.0.0";

class Controller {
 public:
  Controller(std::string host, int port, const std::string& dbConnectionString,
             std::string sidecarUrl, std::string sidecarSecret, const std::string& certPath,
             // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
             const std::string& keyPath, std::string jwtSecret, uint64_t jwtTtlSeconds,
             int rateLimitMaxRequests);

  ~Controller();

  void start();

 private:
  std::string host_;
  int port_;
  std::string sidecarUrl_;
  std::string sidecarSecret_;
  httplib::SSLServer svr_;
  std::unique_ptr<SecurityFilterChain> securityFilterChain_;
  Database db_;
  std::string jwtSecret_;
  uint64_t jwtTtlSeconds_;

  void setupRoutes();
  void healthController_();
  void registerAuthRoutes_();
  void handleSignup_(const httplib::Request& req, httplib::Response& res);
  void handleLogin_(const httplib::Request& req, httplib::Response& res);
  static void handleLogout_(const httplib::Request& req, httplib::Response& res);
  void userController_();
  void publicKeyController_();
  void registerConversationRoutes_();
  void handleCreateConversation_(const httplib::Request& req, httplib::Response& res);
  void handleListUserConversations_(const httplib::Request& req, httplib::Response& res);
  void registerMessageRoutes_();
  void handleCreateMessage_(const httplib::Request& req, httplib::Response& res);
  void handleDeleteMessage_(const httplib::Request& req, httplib::Response& res);
  void handleRevokeMessageAccess_(const httplib::Request& req, httplib::Response& res);
  void handleListConversationMessages_(const httplib::Request& req, httplib::Response& res);
  void basicApiInfo_();
  void registerNotFoundHandler_();
  std::string createSession_(uint64_t userId, const std::string& username) const;
  std::optional<uint64_t> authenticatedUserId_(const httplib::Request& req);
  bool isRateLimited_(const std::string& ipAddr);

  void startBlockchainResolver_();

  struct RateLimitEntry {
    int count;
    std::chrono::steady_clock::time_point windowStart;
  };
  std::unordered_map<std::string, RateLimitEntry> rateLimitMap_;
  std::mutex rateLimitMutex_;
  int rateLimitMaxRequests_;

  std::atomic<bool> stopBlockchainResolver_{false};
  std::thread blockchainResolverThread_;
};

auto configure() -> kd::Controller;

}  // namespace kd
