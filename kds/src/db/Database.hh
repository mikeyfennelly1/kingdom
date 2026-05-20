#pragma once
#include <cstdint>
#include <optional>
#include <pqxx/pqxx>
#include <string>

#include "UserRow.hh"

namespace kd {

class Database {
 public:
  explicit Database(const std::string& connectionString);

  // Insert a new user, returns the new user's id.
  // Throws std::runtime_error if the username is already taken.
  uint64_t createUser(const std::string& username, const std::string& passwordHash);

  // Fetch a user by username. Returns std::nullopt if not found.
  std::optional<UserRow> getUserByUsername(const std::string& username);

 private:
  pqxx::connection conn_;
};

}  // namespace kd
