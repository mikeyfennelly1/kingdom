#include "Server.hh"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string>

int main(void) {
    try {
        // Log Level Configuration
        const char* logLevelEnv = std::getenv("KD_LOG_LEVEL");
        std::string logLevel = logLevelEnv ? logLevelEnv : "info";
        spdlog::set_level(spdlog::level::from_str(logLevel));

        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::info("Initializing Kingdom Server...");

        // Port Configuration
        const char* portEnv = std::getenv("KD_PORT");
        int port = portEnv ? std::stoi(portEnv) : 8080;

        kd::Server server("0.0.0.0", port);
        server.start();
        
    } catch (const std::exception& e) {
        spdlog::critical("Unhandled exception: {}", e.what());
        return 1;
    }

    return 0;
}
