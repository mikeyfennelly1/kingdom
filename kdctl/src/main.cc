#include <CLI/CLI.hpp>
#include <algorithm>
#include <iostream>
#include <kd/Client.hpp>
#include <kd/LocalKeyStore.hpp>
#include <kd/Message.hpp>
#include <kd/MessageStore.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

struct ShellSession {
  bool loggedIn = false;
  uint64_t userId = 0;
  std::string username;
  std::optional<kd::LocalIdentityKey> identityKey;
  kd::MessageStore messageStore;
};

std::string promptLine(const std::string& prompt) {
  std::cout << prompt;
  std::string value;
  std::getline(std::cin, value);
  return value;
}

uint64_t promptId(const std::string& prompt) {
  auto value = promptLine(prompt);
  return std::stoull(value);
}

std::vector<uint64_t> parseIds(const std::string& line) {
  std::vector<uint64_t> ids;
  std::istringstream input(line);
  uint64_t id = 0;
  while (input >> id) {
    ids.push_back(id);
  }
  return ids;
}

void printMessages(const nlohmann::json& msgs) {
  for (const auto& msg : msgs) {
    std::cout << msg.get<kd::Message>().formatted() << std::endl;
  }
}

void printMessages(const nlohmann::json& msgs, kd::Client& client,
                   const kd::LocalIdentityKey& identity) {
  for (const auto& msg : msgs) {
    std::string displayText;
    try {
      auto senderPk = client.getPublicKey(msg["senderId"].get<uint64_t>());
      displayText =
          kd::LocalKeyStore::decryptMessage(msg["payload"].get<std::string>(), identity, senderPk);
    } catch (const std::exception&) {
      displayText = "[decryption failed]";
    }
    std::cout << "[" << msg["timestamp"] << "] "
              << "User " << msg["senderId"] << ": " << displayText << std::endl;
  }
}

void rememberLogin(ShellSession& session, const nlohmann::json& user) {
  if (user.contains("id") && user.contains("username")) {
    session.loggedIn = true;
    session.userId = user["id"];
    session.username = user["username"];
    std::cout << "Logged in as " << session.username << "." << std::endl;
  }
}

void completeLogin(ShellSession& session, kd::Client& client, const nlohmann::json& user,
                   const std::string& username, const std::string& password) {
  try {
    auto identityKey = kd::LocalKeyStore::loadForLogin(username, password);
    rememberLogin(session, user);
    session.identityKey = std::move(identityKey);
    std::cout << "Local private key unlocked." << std::endl;
  } catch (const std::exception& e) {
    client.clearAuthToken();
    session = ShellSession{};
    throw std::runtime_error(
        "Server login succeeded but local private key could not be unlocked: " +
        std::string(e.what()));
  }
}

void printShellHelp() {
  std::cout << "Commands:\n"
            << "  signup\n"
            << "  login\n"
            << "  logout\n"
            << "  health\n"
            << "  info\n"
            << "  conversations\n"
            << "  create-conversation\n"
            << "  send\n"
            << "  messages\n"
            << "  messages-from\n"
            << "  help\n"
            << "  exit\n";
}

void runShell(kd::Client& client, const std::string& serverUrl) {
  ShellSession session;

  std::cout << "Kingdom Control shell" << std::endl;
  std::cout << "server: " << serverUrl << std::endl;
  printShellHelp();

  while (true) {
    std::cout << "\nkdctl";
    if (session.loggedIn) {
      std::cout << ":" << session.username;
    }
    std::cout << "> ";

    std::string command;
    if (!std::getline(std::cin, command)) {
      std::cout << std::endl;
      break;
    }

    try {
      if (command == "exit" || command == "quit") {
        break;
      } else if (command == "help") {
        printShellHelp();
      } else if (command == "health") {
        std::cout << client.getHealth().dump(4) << std::endl;
      } else if (command == "info") {
        std::cout << client.getInfo().dump(4) << std::endl;
      } else if (command == "signup") {
        auto newUsername = promptLine("username: ");
        auto newPassword = promptLine("password: ");
        auto result = client.signup(newUsername, newPassword);
        std::cout << result.dump(4) << std::endl;
        completeLogin(session, client, result, newUsername, newPassword);
      } else if (command == "login") {
        auto loginUsername = promptLine("username: ");
        auto loginPassword = promptLine("password: ");
        auto result = client.login(loginUsername, loginPassword);
        std::cout << result.dump(4) << std::endl;
        completeLogin(session, client, result, loginUsername, loginPassword);
      } else if (command == "logout") {
        std::cout << client.logout().dump(4) << std::endl;
        session = ShellSession{};
      } else if (command == "conversations") {
        if (!session.loggedIn) {
          std::cout << "Log in first." << std::endl;
          continue;
        }
        std::cout << client.getConversations(session.userId).dump(4) << std::endl;
      } else if (command == "create-conversation") {
        auto convName = promptLine("name: ");
        auto participantIds = parseIds(promptLine("participants: "));
        if (session.loggedIn) {
          bool alreadyIncluded =
              std::find_if(participantIds.begin(), participantIds.end(),
                           [&session](uint64_t id) { return id == session.userId; }) !=
              participantIds.end();
          if (!alreadyIncluded) {
            participantIds.push_back(session.userId);
          }
        }
        std::cout << client.createConversation(convName, participantIds).dump(4) << std::endl;
      } else if (command == "send") {
        if (!session.loggedIn || !session.identityKey.has_value()) {
          std::cout << "Log in first." << std::endl;
          continue;
        }
        auto convId = promptId("conversation id: ");
        auto recipientId = promptId("recipient user id: ");
        auto messageText = promptLine("message: ");
        auto recipientPk = client.getPublicKey(recipientId);
        auto payload =
            kd::LocalKeyStore::encryptMessage(messageText, *session.identityKey, recipientPk);
        std::cout << client.sendMessage(convId, session.userId, payload).dump(4) << std::endl;
      } else if (command == "messages") {
        auto convId = promptId("conversation id: ");
        auto msgs = client.getMessages(convId);
        std::sort(msgs.begin(), msgs.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
          return a["timestamp"].get<uint64_t>() < b["timestamp"].get<uint64_t>();
        });
        session.messageStore.clear();
        for (const auto& m : msgs) {
          session.messageStore.add(m.get<kd::Message>());
        }
        if (session.loggedIn && session.identityKey.has_value()) {
          printMessages(msgs, client, *session.identityKey);
        } else {
          printMessages(msgs);
        }
      } else if (command == "messages-from") {
        auto senderId = promptId("sender user id: ");
        auto results = session.messageStore.findBySender(senderId);
        if (results.empty()) {
          std::cout << "No cached messages from user " << senderId
                    << ". Run 'messages' first." << std::endl;
        } else {
          for (const auto& m : results) {
            std::cout << m.formatted() << std::endl;
          }
        }
      } else if (!command.empty()) {
        std::cout << "Unknown command. Type help for commands." << std::endl;
      }
    } catch (const std::exception& e) {
      std::cout << "Error: " << e.what() << std::endl;
    }
  }
}

int main(int argc, char** argv) {
  CLI::App app{"Kingdom Control - CLI for Kingdom Server"};

  std::string host = "localhost";
  int port = 8080;
  std::string protocol = "http";
  std::string serverUrl;
  std::string caCertPath;

  app.add_option("-H,--host", host, "Server host")->envname("KD_HOST")->capture_default_str();
  app.add_option("-p,--port", port, "Server port")->envname("KD_PORT")->capture_default_str();
  app.add_option("-P,--protocol", protocol, "Server protocol (http/https)")
      ->envname("KD_PROTOCOL")
      ->capture_default_str();
  app.add_option("-s,--server", serverUrl, "Full Server URL (overrides host/port/protocol)");
  app.add_option("-c,--ca-cert", caCertPath, "Path to CA certificate for TLS verification")
      ->envname("KD_CA_CERT");

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
  createConvCmd->add_option("-p,--participant", participantIds, "Participant user ID (repeatable)")
      ->required();

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
    kd::Client client(serverUrl, caCertPath);

    if (app.get_subcommands().empty()) {
      runShell(client, serverUrl);
    } else if (healthCmd->parsed()) {
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
      printMessages(client.getMessages(msgsConvId));
    } else {
      std::cout << app.help() << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
