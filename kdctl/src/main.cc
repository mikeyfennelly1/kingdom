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
  std::vector<kd::Message> messageCache;
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
  try {
    return std::stoull(value);
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("invalid id: '" + value + "' is not a number");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("invalid id: '" + value + "' is out of range");
  }
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

void printMessages(const nlohmann::json& msgs, kd::Client& client, kd::LocalIdentityKey& identity,
                   uint64_t recipientId, kd::MessageStore& store) {
  for (const auto& msg : msgs) {
    const auto message = msg.get<kd::Message>();
    std::string displayText;

    if (auto cachedPlaintext = store.getPlaintext(message.id); cachedPlaintext.has_value()) {
      displayText = *cachedPlaintext;
    } else {
      try {
        auto cached = store.getCachedPublicKey(message.senderId);
        std::string senderPk = cached.has_value() ? *cached : client.getPublicKey(message.senderId);
        store.cachePublicKey(message.senderId, senderPk);
        displayText = kd::LocalKeyStore::decryptMessage(message.payload, identity, senderPk,
                                                        message.conversationId, message.senderId,
                                                        recipientId);
        store.savePlaintext(message.id, message.conversationId, message.senderId,
                            message.timestamp, displayText);
      } catch (const std::exception&) {
        displayText = "[decryption failed]";
      }
    }

    std::cout << "[" << message.timestamp << "] "
              << "User " << message.senderId << ": " << displayText << std::endl;
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
    session.messageStore = kd::MessageStore(username);
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
            << "  delete-message\n"
            << "  help\n"
            << "  exit\n";
}

void runShell(kd::Client& client, const std::string& serverUrl) {
  ShellSession session;

  try {
    client.getHealth();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return;
  }

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
              std::find_if(participantIds.begin(), participantIds.end(), [&session](uint64_t id) {
                return id == session.userId;
              }) != participantIds.end();
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
            kd::LocalKeyStore::encryptMessage(messageText, *session.identityKey, recipientPk,
                                              convId, session.userId, recipientId);
        auto sentMessageJson = client.sendMessage(convId, session.userId, payload);
        std::cout << sentMessageJson.dump(4) << std::endl;
        auto sentMessage = sentMessageJson.get<kd::Message>();
        session.messageStore.savePlaintext(sentMessage.id, sentMessage.conversationId,
                                           sentMessage.senderId, sentMessage.timestamp,
                                           messageText);
        session.messageCache.push_back(sentMessage);
        auto usedPreKeyId = kd::LocalKeyStore::oneTimePreKeyIdFromPayload(payload);
        if (usedPreKeyId.has_value()) {
          client.consumeOneTimePreKey(recipientId, *usedPreKeyId);
        }
      } else if (command == "messages") {
        auto convId = promptId("conversation id: ");
        auto msgs = client.getMessages(convId);
        std::sort(msgs.begin(), msgs.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
          return a["timestamp"].get<uint64_t>() < b["timestamp"].get<uint64_t>();
        });
        session.messageCache.clear();
        for (const auto& m : msgs) {
          session.messageCache.push_back(m.get<kd::Message>());
        }
        if (session.loggedIn && session.identityKey.has_value()) {
          printMessages(msgs, client, *session.identityKey, session.userId, session.messageStore);
        } else {
          printMessages(msgs);
        }
      } else if (command == "delete-message") {
        if (!session.loggedIn) {
          std::cout << "Log in first." << std::endl;
          continue;
        }
        auto convId = promptId("conversation id: ");
        auto messageId = promptId("message id: ");
        auto response = client.deleteMessage(convId, messageId);
        session.messageStore.deletePlaintext(messageId);
        session.messageCache.erase(
            std::remove_if(session.messageCache.begin(), session.messageCache.end(),
                           [messageId](const kd::Message& message) {
                             return message.id == messageId;
                           }),
            session.messageCache.end());
        std::cout << response.dump(4) << std::endl;
      } else if (command == "messages-from") {
        auto senderId = promptId("sender user id: ");
        std::vector<kd::Message> results;
        std::copy_if(session.messageCache.begin(), session.messageCache.end(),
                     std::back_inserter(results),
                     [senderId](const kd::Message& m) { return m.senderId == senderId; });
        if (results.empty()) {
          std::cout << "No cached messages from user " << senderId << ". Run 'messages' first."
                    << std::endl;
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
    } else {
      std::cout << app.help() << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
