#pragma once
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "../common/Constants.hh"
#include "JwtUtils.hh"
#include "SecurityPredicate.hh"

namespace kd {

// In-memory blacklist for revoked JWT tokens. Populated on logout.
// Tokens are stored verbatim and checked in ValidateAuthenticated.
class TokenBlacklist {
 public:
  static void revoke(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_.insert(token);
  }

  static bool isRevoked(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    return tokens_.count(token) > 0;
  }

 private:
  inline static std::unordered_set<std::string> tokens_;
  inline static std::mutex mutex_;
};

class ValidateSenderAuthenticity : public SecurityPredicate {
 public:
  auto Validate(const httplib::Request& req) -> std::optional<SecurityError> override {
    spdlog::debug("Executing SecurityPredicate: ValidateSenderAuthenticity");

    if (req.method != "POST") {
      return std::nullopt;
    }

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("senderId")) {
      return std::nullopt;
    }

    auto token = bearerToken(req);
    if (!token.has_value()) {
      return std::nullopt;
    }

    const char* secret = std::getenv("KD_JWT_SECRET");
    if (secret == nullptr) {
      return std::nullopt;
    }

    auto payload = verifiedJwtPayload(*token, std::string(secret));
    if (!payload.has_value()) {
      return std::nullopt;
    }

    try {
      uint64_t authenticatedUserId = std::stoull((*payload)["sub"].get<std::string>());
      uint64_t senderId = body["senderId"].get<uint64_t>();
      if (authenticatedUserId != senderId) {
        return SecurityError{"sender ID does not match authenticated user", 403};
      }
    } catch (const std::exception&) {
      return std::nullopt;
    }

    return std::nullopt;
  }
};

class ValidateUntampered : public SecurityPredicate {
 public:
  auto Validate(const httplib::Request& req) -> std::optional<SecurityError> override {
    spdlog::debug("Executing SecurityPredicate: ValidateUntampered");

    if (req.method != "POST" && req.method != "PUT") {
      return std::nullopt;
    }

    const auto contentType = req.get_header_value(http_headers::ContentType);
    if (contentType.empty() || contentType.find(content_types::Json) == std::string::npos) {
      return SecurityError{"Content-Type must be application/json", 400};
    }

    return std::nullopt;
  }
};

class ValidateAuthenticated : public SecurityPredicate {
 public:
  auto Validate(const httplib::Request& req) -> std::optional<SecurityError> override {
    spdlog::debug("Executing SecurityPredicate: ValidateAuthenticated");

    // Public routes that do not require authentication
    static const std::vector<std::string> kPublicPaths = {routes::Login, routes::Signup,
                                                          routes::Health, routes::Root};
    for (const auto& path : kPublicPaths) {
      if (req.path == path) {
        return std::nullopt;
      }
    }
    static const std::regex kPublicKeyPath(routes::PublicUserKeyPathRegex);
    if (std::regex_match(req.path, kPublicKeyPath)) {
      return std::nullopt;
    }
    auto token = bearerToken(req);
    if (!token.has_value()) {
      return SecurityError{"authentication required", 401};
    }
    const char* secret = std::getenv("KD_JWT_SECRET");
    if (secret == nullptr) {
      return SecurityError{"server misconfiguration: JWT secret not set", 500};
    }
    if (!verifiedJwtPayload(*token, std::string(secret)).has_value()) {
      return SecurityError{"invalid or expired session token", 401};
    }
    if (TokenBlacklist::isRevoked(*token)) {
      return SecurityError{"session has been revoked", 401};
    }
    return std::nullopt;
  }
};

}  // namespace kd
