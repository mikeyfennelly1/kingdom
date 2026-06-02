
#include <httplib.h>

#include <cstdlib>
#include <kd/Conversation.hpp>
#include <stdexcept>

#include "Controller.hh"

namespace kd {

namespace {
constexpr std::size_t kMinJwtSecretLen = 32;
constexpr uint64_t kDefaultJwtTtlSeconds = 3600;
constexpr int kDefaultRateLimitMaxRequests = 10;
}  // namespace

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
  spdlog::info("TLS cert: {}", certPath);
  spdlog::info("TLS key:  {}", keyPath);

  const char* jwtSecret = std::getenv("KD_JWT_SECRET");
  if (jwtSecret == nullptr || std::string(jwtSecret).size() < kMinJwtSecretLen) {
    throw std::runtime_error("KD_JWT_SECRET must be set to at least 32 characters");
  }

  const char* jwtTtlEnv = std::getenv("KD_JWT_TTL_SECONDS");
  uint64_t jwtTtlSeconds = (jwtTtlEnv != nullptr) ? std::stoull(jwtTtlEnv) : kDefaultJwtTtlSeconds;
  if (jwtTtlSeconds == 0) {
    throw std::runtime_error("KD_JWT_TTL_SECONDS must be greater than zero");
  }

  const char* rateLimitEnv = std::getenv("KD_RATE_LIMIT_MAX_REQUESTS");
  int rateLimitMaxRequests = (rateLimitEnv != nullptr) ? std::stoi(rateLimitEnv)
                                                       : kDefaultRateLimitMaxRequests;

  return {"0.0.0.0",   port,       dbUrl,      sidecarUrl,          certPath,
          keyPath,     jwtSecret,  jwtTtlSeconds, rateLimitMaxRequests};
}
}  // namespace kd
