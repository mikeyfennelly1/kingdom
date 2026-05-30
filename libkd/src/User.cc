#include <kd/User.hpp>

#include <sodium.h>

#include <stdexcept>

namespace kd {

User::User(uint64_t id, std::string username, std::string passwordHash, std::string publicKey)
    : id_(id)
    , username_(std::move(username))
    , passwordHash_(std::move(passwordHash))
    , publicKey_(std::move(publicKey)) {}

uint64_t User::id() const {
    return id_;
}

const std::string& User::username() const {
    return username_;
}

const std::string& User::passwordHash() const {
    return passwordHash_;
}

const std::string& User::publicKey() const {
    return publicKey_;
}

bool User::verifyPassword(const std::string& password) const {
    return crypto_pwhash_str_verify(passwordHash_.c_str(), password.c_str(), password.size()) == 0;
}

std::string User::hashPassword(const std::string& password) {
    char encodedHash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str_alg(encodedHash, password.c_str(), password.size(),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("Failed to hash password");
    }
    return encodedHash;
}

}  // namespace kd
