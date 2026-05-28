
#include <httplib.h>

#include <cstdlib>
#include <kd/Conversation.hpp>
#include <stdexcept>

#include "Controller.hh"

namespace kd {
auto configure() -> kd::Controller {
  // Log Level Configuration
  const char* logLevelEnv = std::getenv("KD_LOG_LEVEL");
  std::string logLevel = (logLevelEnv != nullptr) ? logLevelEnv : "info";
  spdlog::set_level(spdlog::level::from_str(logLevel));

  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
  spdlog::info("Initializing Kingdom Server...");

  // Port Configuration
  const char* portEnv = std::getenv("KD_PORT");
  int port = (portEnv != nullptr) ? std::stoi(portEnv) : defaultPortNumber;

  // Database Configuration
  const char* dbUrl = std::getenv("KD_DB_URL");
  if (dbUrl == nullptr) {
    throw std::runtime_error("KD_DB_URL environment variable not set");
  }

  const char* sidecarEnv = std::getenv("KD_BLOCKCHAIN_SIDECAR_URL");
  std::string sidecarUrl = (sidecarEnv != nullptr) ? sidecarEnv : "http://localhost:3001";

  const char* certPath = std::getenv("KD_TLS_CERT");
  const char* keyPath = std::getenv("KD_TLS_KEY");
  if (certPath == nullptr || keyPath == nullptr) {
    throw std::runtime_error("KD_TLS_CERT and KD_TLS_KEY environment variables must be set");
  }

  const char* jwtSecret = std::getenv("KD_JWT_SECRET");
  if (jwtSecret == nullptr || std::string(jwtSecret).size() < 32) {
    throw std::runtime_error("KD_JWT_SECRET must be set to at least 32 characters");
  }

  const char* jwtTtlEnv = std::getenv("KD_JWT_TTL_SECONDS");
  uint64_t jwtTtlSeconds = (jwtTtlEnv != nullptr) ? std::stoull(jwtTtlEnv) : 3600;
  if (jwtTtlSeconds == 0) {
    throw std::runtime_error("KD_JWT_TTL_SECONDS must be greater than zero");
  }

  const char* frontendPathEnv = std::getenv("KD_FRONTEND_PATH");
  std::string frontendPath = (frontendPathEnv != nullptr) ? frontendPathEnv : "";

  return {"0.0.0.0", port, dbUrl, sidecarUrl, certPath, keyPath, jwtSecret, jwtTtlSeconds,
          frontendPath};
}
}  // namespace kd
