
#include <httplib.h>

#include <kd/Conversation.hpp>

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
  return {"0.0.0.0", port};
}
}  // namespace kd
