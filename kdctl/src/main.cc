#include <CLI/CLI.hpp>
#include <iostream>
#include <kd/Client.hpp>

int main(int argc, char** argv) {
    CLI::App app{"Kingdom Control - CLI for Kingdom Server"};

    std::string serverUrl = "http://localhost:8080";
    app.add_option("-s,--server", serverUrl, "Server base URL")->capture_default_str();

    auto healthCmd = app.add_subcommand("health", "Check server health");
    auto infoCmd = app.add_subcommand("info", "Get server information");

    CLI11_PARSE(app, argc, argv);

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
