#pragma once
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>

#include "JwtUtils.hh"
#include "SecurityPredicate.hh"

namespace kd {

class ValidateSenderAuthenticity : public SecurityPredicate {
 public:
  auto Validate(const httplib::Request& req) -> std::optional<SecurityError> override {
    spdlog::debug("Executing SecurityPredicate: ValidateSenderAuthenticity");
    return std::nullopt;  // Stub
  }
};

class ValidateUntampered : public SecurityPredicate {
 public:
  auto Validate(const httplib::Request& req) -> std::optional<SecurityError> override {
    spdlog::debug("Executing SecurityPredicate: ValidateUntampered");
    return std::nullopt;  // Stub
  }
};

class ValidateAuthenticated : public SecurityPredicate {
 public:
  auto Validate(const httplib::Request& req) -> std::optional<SecurityError> override {
    spdlog::debug("Executing SecurityPredicate: ValidateAuthenticated");
    auto token = bearerToken(req);
    if (!token.has_value()) {
      return std::nullopt;  // No token present — individual handlers enforce if required
    }
    const char* secret = std::getenv("KD_JWT_SECRET");
    if (secret == nullptr) {
      return SecurityError{"server misconfiguration: JWT secret not set", 500};
    }
    if (!verifiedJwtPayload(*token, std::string(secret)).has_value()) {
      return SecurityError{"invalid or expired session token", 401};
    }
    return std::nullopt;
  }
};

}  // namespace kd
