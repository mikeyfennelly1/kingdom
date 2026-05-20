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
    
    auto convsCmd = app.add_subcommand("conversations", "List conversations for a user");
    uint64_t userId = 0;
    convsCmd->add_option("-u,--user-id", userId, "User ID to fetch conversations for")->required();

    auto signupCmd = app.add_subcommand("signup", "Register a new user");
    std::string username, password;
    signupCmd->add_option("-u,--username", username, "Username")->required();
    signupCmd->add_option("-p,--password", password, "Password")->required();

    auto loginCmd = app.add_subcommand("login", "Log in as a user");
    loginCmd->add_option("-u,--username", username, "Username")->required();
    loginCmd->add_option("-p,--password", password, "Password")->required();

    auto logoutCmd = app.add_subcommand("logout", "Log out current session");

    auto createConvCmd = app.add_subcommand("create-conversation", "Create a new conversation");
    std::string convName;
    std::vector<uint64_t> participantIds;
    createConvCmd->add_option("-n,--name", convName, "Conversation name")->required();
    createConvCmd->add_option("-p,--participant", participantIds, "Participant user ID (repeatable)")->required();

    auto sendCmd = app.add_subcommand("send", "Send a message to a conversation");
    uint64_t convId = 0;
    uint64_t senderId = 0;
    std::string messageText;
    sendCmd->add_option("-c,--conversation-id", convId, "Conversation ID")->required();
    sendCmd->add_option("-u,--sender-id", senderId, "Sender user ID")->required();
    sendCmd->add_option("-m,--message", messageText, "Message text")->required();

    auto msgsCmd = app.add_subcommand("messages", "List messages in a conversation");
    uint64_t msgsConvId = 0;
    msgsCmd->add_option("-c,--conversation-id", msgsConvId, "Conversation ID")->required();

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
        } else if (convsCmd->parsed()) {
            std::cout << client.getConversations(userId).dump(4) << std::endl;
        } else if (signupCmd->parsed()) {
            std::cout << client.signup(username, password).dump(4) << std::endl;
        } else if (loginCmd->parsed()) {
            std::cout << client.login(username, password).dump(4) << std::endl;
        } else if (logoutCmd->parsed()) {
            std::cout << client.logout().dump(4) << std::endl;
        } else if (createConvCmd->parsed()) {
            std::cout << client.createConversation(convName, participantIds).dump(4) << std::endl;
        } else if (sendCmd->parsed()) {
            std::cout << client.sendMessage(convId, senderId, messageText).dump(4) << std::endl;
        } else if (msgsCmd->parsed()) {
            auto msgs = client.getMessages(msgsConvId);
            for (const auto& msg : msgs) {
                std::cout << "[" << msg["timestamp"] << "] "
                          << "User " << msg["senderId"] << ": "
                          << msg["payload"] << std::endl;
            }
        } else {
            std::cout << app.help() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
