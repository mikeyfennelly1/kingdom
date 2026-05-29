#pragma once
#include <cstdint>
#include <string>

namespace kd {

class User {
public:
    User(uint64_t id, std::string username, std::string passwordHash = "",
         std::string publicKey = "");

    [[nodiscard]] uint64_t id() const;
    [[nodiscard]] const std::string& username() const;
    [[nodiscard]] const std::string& passwordHash() const;
    [[nodiscard]] const std::string& publicKey() const;

    [[nodiscard]] bool verifyPassword(const std::string& password) const;
    [[nodiscard]] static std::string hashPassword(const std::string& password);

private:
    uint64_t id_;
    std::string username_;
    std::string passwordHash_;
    std::string publicKey_;
};

}  // namespace kd
