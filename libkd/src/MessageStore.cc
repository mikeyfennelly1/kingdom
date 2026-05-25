#include "kd/MessageStore.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>

namespace kd {
namespace {

std::string sanitizedUsername(const std::string& username) {
  std::string value;
  value.reserve(username.size());
  for (char ch : username) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_') {
      value.push_back(ch);
    } else {
      value.push_back('_');
    }
  }
  return value.empty() ? "user" : value;
}

std::filesystem::path defaultStorePath(const std::string& username) {
  const char* home = std::getenv("HOME");
  if (home == nullptr || std::string(home).empty()) {
    throw std::runtime_error("HOME environment variable not set");
  }
  return std::filesystem::path(home) / ".kingdom" / "messages" /
         (sanitizedUsername(username) + ".json");
}

nlohmann::json emptyStore() {
  return nlohmann::json{{"version", 1}, {"messages", nlohmann::json::object()}};
}

nlohmann::json readStore(const std::filesystem::path& path) {
  if (path.empty() || !std::filesystem::exists(path)) {
    return emptyStore();
  }

  std::ifstream input(path);
  auto parsed = nlohmann::json::parse(input, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return emptyStore();
  }
  if (!parsed.contains("messages") || !parsed["messages"].is_object()) {
    parsed["messages"] = nlohmann::json::object();
  }
  return parsed;
}

void writeStore(const std::filesystem::path& path, const nlohmann::json& store) {
  if (path.empty()) {
    return;
  }

  std::filesystem::create_directories(path.parent_path());
  const auto tmpPath = path.string() + ".tmp";
  {
    std::ofstream output(tmpPath, std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Failed to open local message store for writing: " + path.string());
    }
    output << store.dump(2) << '\n';
  }

  std::filesystem::rename(tmpPath, path);
  std::error_code ignored;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ignored);
}

}  // namespace

MessageStore::MessageStore(const std::string& username) : storePath_(defaultStorePath(username)) {}

MessageStore::MessageStore(std::filesystem::path storePath) : storePath_(std::move(storePath)) {}

void MessageStore::add(Message message) {
  messages_.push_back(std::move(message));
}

const std::vector<Message>& MessageStore::getAll() const {
  return messages_;
}

std::vector<Message> MessageStore::findBySender(uint64_t senderId) const {
  std::vector<Message> result;
  std::copy_if(messages_.begin(), messages_.end(), std::back_inserter(result),
               [senderId](const Message& message) { return message.senderId == senderId; });
  return result;
}

void MessageStore::clear() {
  messages_.clear();
}

void MessageStore::cachePublicKey(uint64_t userId, const std::string& publicKey) {
  publicKeyCache_[userId] = publicKey;
}

std::optional<std::string> MessageStore::getCachedPublicKey(uint64_t userId) const {
  auto it = publicKeyCache_.find(userId);
  if (it == publicKeyCache_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> MessageStore::getPlaintext(uint64_t messageId) const {
  const auto store = readStore(storePath_);
  const auto key = std::to_string(messageId);
  if (!store["messages"].contains(key)) {
    return std::nullopt;
  }

  const auto& record = store["messages"][key];
  if (!record.is_object() || !record.contains("plaintext") || !record["plaintext"].is_string()) {
    return std::nullopt;
  }
  return record["plaintext"].get<std::string>();
}

void MessageStore::savePlaintext(uint64_t messageId, uint64_t conversationId, uint64_t senderId,
                                 uint64_t timestamp, const std::string& plaintext) const {
  auto store = readStore(storePath_);
  store["messages"][std::to_string(messageId)] = {{"messageId", messageId},
                                                   {"conversationId", conversationId},
                                                   {"senderId", senderId},
                                                   {"timestamp", timestamp},
                                                   {"plaintext", plaintext}};
  writeStore(storePath_, store);
}

}  // namespace kd
