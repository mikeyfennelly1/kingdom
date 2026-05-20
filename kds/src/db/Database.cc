#include "Database.hh"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace kd {

Database::Database(const std::string& connectionString) : conn_(connectionString) {
    spdlog::info("Connected to database");
}

uint64_t Database::createUser(const std::string& username, const std::string& passwordHash) {
    try {
        pqxx::work txn(conn_);
        pqxx::params params{username, passwordHash};
        auto result = txn.exec(
            "INSERT INTO users (username, password_hash) VALUES ($1, $2) RETURNING id",
            params
        );
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
        params
    );
    txn.commit();

    if (result.empty()) {
        return std::nullopt;
    }

    return UserRow{
        result[0][0].as<uint64_t>(),
        result[0][1].as<std::string>(),
        result[0][2].as<std::string>(),
        result[0][3].as<std::string>()
    };
}

}  // namespace kd
