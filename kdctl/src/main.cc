#include <CLI/CLI.hpp>
#include <iostream>
#include <kd/Client.hpp>

int main(int argc, char** argv) {
    CLI::App app{"Kingdom Control - CLI for Kingdom Server"};

    std::string host = "localhost";
    int port = 8080;
    std::string protocol = "http";
    std::string serverUrl;

    app.add_option("-H,--host", host, "Server host")
        ->envname("KD_HOST")
        ->capture_default_str();
    app.add_option("-p,--port", port, "Server port")
        ->envname("KD_PORT")
        ->capture_default_str();
    app.add_option("-P,--protocol", protocol, "Server protocol (http/https)")
        ->envname("KD_PROTOCOL")
        ->capture_default_str();
    app.add_option("-s,--server", serverUrl, "Full Server URL (overrides host/port/protocol)");

    auto healthCmd = app.add_subcommand("health", "Check server health");
    auto infoCmd = app.add_subcommand("info", "Get server information");

    CLI11_PARSE(app, argc, argv);

    // Construct URL if not explicitly provided via --server
    if (serverUrl.empty()) {
        serverUrl = protocol + "://" + host + ":" + std::to_string(port);
    }

    try {
        kd::Client client(serverUrl);

        if (healthCmd->parsed()) {
            std::cout << client.getHealth().dump(4) << std::endl;
        } else if (infoCmd->parsed()) {
            std::cout << client.getInfo().dump(4) << std::endl;
        } else {
            std::cout << app.help() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
