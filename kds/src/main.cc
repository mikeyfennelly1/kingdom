#include "Server.hh"
#include <spdlog/spdlog.h>

int main(void) {
    try {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::info("Initializing Kingdom Server...");

        kd::Server server("0.0.0.0", 8080);
        server.start();
        
    } catch (const std::exception& e) {
        spdlog::critical("Unhandled exception: {}", e.what());
        return 1;
    }

    return 0;
}
