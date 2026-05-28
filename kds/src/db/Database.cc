#include "Database.hh"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace kd {

Database::Database(const std::string& connectionString) : conn_(connectionString) {
  spdlog::info("Connected to database");
  initSchema_();
}

void Database::initSchema_() {
  pqxx::work txn(conn_);
  txn.exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id BIGSERIAL PRIMARY KEY,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            public_key TEXT
        )
    )");
  txn.exec(R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id BIGSERIAL PRIMARY KEY,
            name TEXT NOT NULL,
            created_at BIGINT NOT NULL
        )
    )");
  txn.exec(R"(
        CREATE TABLE IF NOT EXISTS conversation_participants (
            conversation_id BIGINT NOT NULL REFERENCES conversations(id),
            user_id BIGINT NOT NULL REFERENCES users(id),
            PRIMARY KEY (conversation_id, user_id)
        )
    )");
  txn.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id BIGSERIAL PRIMARY KEY,
            conversation_id BIGINT NOT NULL REFERENCES conversations(id),
            sender_id BIGINT NOT NULL REFERENCES users(id),
            payload TEXT NOT NULL,
            timestamp BIGINT NOT NULL,
            blockchain_digest TEXT NOT NULL DEFAULT ''
        )
    )");
  txn.commit();
  spdlog::info("Database schema initialized");
}

uint64_t Database::createUser(const std::string& username, const std::string& passwordHash,
                              const std::string& publicKey) {
  try {
    pqxx::work txn(conn_);
    pqxx::params params{username, passwordHash, publicKey};
    auto result = txn.exec(
        "INSERT INTO users (username, password_hash, public_key) VALUES ($1, $2, $3) RETURNING id",
        params);
    txn.commit();
    return result[0][0].as<uint64_t>();
  } catch (const pqxx::unique_violation&) {
    throw std::runtime_error("Username already taken");
  }
}

std::optional<UserRow> Database::getUserByUsername(const std::string& username) {
  pqxx::work txn(conn_);
  pqxx::params params{username};
  auto result = txn.exec(
      "SELECT id, username, password_hash, COALESCE(public_key, '') FROM users WHERE username = $1",
      params);
  txn.commit();

  if (result.empty()) {
    return std::nullopt;
  }

  return UserRow{result[0][0].as<uint64_t>(), result[0][1].as<std::string>(),
                 result[0][2].as<std::string>(), result[0][3].as<std::string>()};
}

std::optional<std::string> Database::getUserPublicKey(uint64_t userId) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(userId)};
  auto result = txn.exec("SELECT COALESCE(public_key, '') FROM users WHERE id = $1", params);
  txn.commit();

  if (result.empty()) {
    return std::nullopt;
  }
  return result[0][0].as<std::string>();
}

bool Database::consumeOneTimePreKey(uint64_t userId, uint64_t preKeyId) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(userId)};
  auto result =
      txn.exec("SELECT COALESCE(public_key, '') FROM users WHERE id = $1 FOR UPDATE", params);
  if (result.empty()) {
    txn.commit();
    return false;
  }

  auto bundle = nlohmann::json::parse(result[0][0].as<std::string>(), nullptr, false);
  if (bundle.is_discarded() || !bundle.is_object() || !bundle.contains("oneTimePreKeys") ||
      !bundle["oneTimePreKeys"].is_array()) {
    txn.commit();
    return false;
  }

  auto& oneTimePreKeys = bundle["oneTimePreKeys"];
  const auto originalSize = oneTimePreKeys.size();
  oneTimePreKeys.erase(std::remove_if(oneTimePreKeys.begin(), oneTimePreKeys.end(),
                                      [preKeyId](const auto& key) {
                                        return key.is_object() && key.contains("id") &&
                                               key["id"].template get<uint64_t>() == preKeyId;
                                      }),
                       oneTimePreKeys.end());

  if (oneTimePreKeys.size() == originalSize) {
    txn.commit();
    return false;
  }

  pqxx::params updateParams{bundle.dump(), static_cast<int64_t>(userId)};
  txn.exec("UPDATE users SET public_key = $1 WHERE id = $2", updateParams);
  txn.commit();
  return true;
}

uint64_t Database::createConversation(const std::string& name,
                                      const std::vector<uint64_t>& participantIds) {
  pqxx::work txn(conn_);

  auto now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());

  pqxx::params convParams{name, now};
  auto result =
      txn.exec("INSERT INTO conversations (name, created_at) VALUES ($1, $2) RETURNING id",
               convParams);
  uint64_t convId = result[0][0].as<uint64_t>();

  for (auto uid : participantIds) {
    pqxx::params pParams{static_cast<int64_t>(convId), static_cast<int64_t>(uid)};
    txn.exec("INSERT INTO conversation_participants (conversation_id, user_id) VALUES ($1, $2)",
             pParams);
  }

  txn.commit();
  return convId;
}

std::vector<kd::Conversation> Database::getConversationsByUserId(uint64_t userId) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(userId)};
  auto result = txn.exec(
      "SELECT c.id, c.name, c.created_at, array_agg(cp2.user_id) AS participant_ids "
      "FROM conversations c "
      "JOIN conversation_participants cp ON c.id = cp.conversation_id "
      "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
      "WHERE cp.user_id = $1 "
      "GROUP BY c.id, c.name, c.created_at",
      params);
  txn.commit();

  std::vector<kd::Conversation> conversations;
  for (const auto& row : result) {
    kd::Conversation conv;
    conv.id = row[0].as<uint64_t>();
    conv.name = row[1].as<std::string>();
    conv.createdAt = row[2].as<uint64_t>();

    // Parse PostgreSQL array string e.g. {1,2,3}
    std::string arrayStr = row[3].as<std::string>();
    if (arrayStr.size() >= 2) {
      std::string_view inner{arrayStr.data() + 1, arrayStr.size() - 2};
      while (!inner.empty()) {
        auto comma = inner.find(',');
        std::string_view token = (comma == std::string_view::npos) ? inner : inner.substr(0, comma);
        uint64_t participantId{};
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), participantId);
        if (ec == std::errc{}) {
          conv.participantIds.push_back(participantId);
        }
        if (comma == std::string_view::npos) {
          break;
        }
        inner = inner.substr(comma + 1);
      }
    }

    conversations.push_back(std::move(conv));
  }
  return conversations;
}

uint64_t Database::createMessage(uint64_t conversationId, uint64_t senderId,
                                 const std::string& payload, uint64_t timestamp) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(conversationId), static_cast<int64_t>(senderId), payload,
                      static_cast<int64_t>(timestamp)};
  auto result = txn.exec(
      "INSERT INTO messages (conversation_id, sender_id, payload, timestamp) "
      "VALUES ($1, $2, $3, $4) RETURNING id",
      params);
  txn.commit();
  return result[0][0].as<uint64_t>();
}

std::vector<kd::Message> Database::getMessagesByConversationId(uint64_t conversationId) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(conversationId)};
  auto result = txn.exec(
      "SELECT id, sender_id, conversation_id, payload, timestamp, blockchain_digest "
      "FROM messages WHERE conversation_id = $1 ORDER BY timestamp ASC",
      params);
  txn.commit();

  std::vector<kd::Message> messages;
  for (const auto& row : result) {
    kd::Message msg;
    msg.id = row[0].as<uint64_t>();
    msg.senderId = row[1].as<uint64_t>();
    msg.conversationId = row[2].as<uint64_t>();
    msg.payload = row[3].as<std::string>();
    msg.signature = "";
    msg.timestamp = row[4].as<uint64_t>();
    msg.blockchainDigest = row[5].as<std::string>();
    messages.push_back(std::move(msg));
  }
  return messages;
}

bool Database::deleteMessage(uint64_t conversationId, uint64_t messageId, uint64_t senderId) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(messageId), static_cast<int64_t>(conversationId),
                      static_cast<int64_t>(senderId)};
  auto result = txn.exec(
      "DELETE FROM messages WHERE id = $1 AND conversation_id = $2 AND sender_id = $3",
      params);
  txn.commit();
  return result.affected_rows() == 1;
}

void Database::updateMessageBlockchainDigest(uint64_t msgId, const std::string& digest) {
  pqxx::work txn(conn_);
  pqxx::params params{digest, static_cast<int64_t>(msgId)};
  txn.exec("UPDATE messages SET blockchain_digest = $1 WHERE id = $2", params);
  txn.commit();
}

bool Database::isParticipant(uint64_t conversationId, uint64_t userId) {
  pqxx::work txn(conn_);
  pqxx::params params{static_cast<int64_t>(conversationId), static_cast<int64_t>(userId)};
  auto result = txn.exec(
      "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 AND user_id = $2",
      params);
  txn.commit();
  return !result.empty();
}

}  // namespace kd
