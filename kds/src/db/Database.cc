#include "Database.hh"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace kd {

Database::Database(const std::string& connectionString) : conn_(connectionString) {
    spdlog::info("Connected to database");
}

uint64_t Database::createUser(const std::string& username, const std::string& passwordHash) {
    spdlog::debug("Database: Creating user '{}'", username);
    try {
        pqxx::work txn(conn_);
        pqxx::params params{username, passwordHash};
        auto result =
            txn.exec("INSERT INTO users (username, password_hash) VALUES ($1, $2) RETURNING id",
                     params);
        txn.commit();
        uint64_t id = result[0][0].as<uint64_t>();
        spdlog::debug("Database: Successfully created user '{}' with id {}", username, id);
        return id;
    } catch (const pqxx::unique_violation&) {
        spdlog::debug("Database: Failed to create user '{}' - username already taken", username);
        throw std::runtime_error("Username already taken");
    }
}

std::optional<UserRow> Database::getUserByUsername(const std::string& username) {
    spdlog::debug("Database: Looking up user by username '{}'", username);
    pqxx::work txn(conn_);
    pqxx::params params{username};
    auto result = txn.exec(
        "SELECT id, username, password_hash, COALESCE(public_key, '') FROM users WHERE "
        "username = $1",
        params);
    txn.commit();

    if (result.empty()) {
        spdlog::debug("Database: User '{}' not found", username);
        return std::nullopt;
    }

    spdlog::debug("Database: Found user '{}' with id {}", username, result[0][0].as<uint64_t>());
    return UserRow{result[0][0].as<uint64_t>(), result[0][1].as<std::string>(),
                   result[0][2].as<std::string>(), result[0][3].as<std::string>()};
}

}  // namespace kd
