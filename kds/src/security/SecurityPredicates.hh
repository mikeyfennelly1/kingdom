#pragma once
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>
#include <nlohmann/json.hpp>

#include "JwtUtils.hh"
#include "SecurityPredicate.hh"

namespace kd {

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

    const auto contentType = req.get_header_value("Content-Type");
    if (contentType.empty() || contentType.find("application/json") == std::string::npos) {
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
    static const std::vector<std::string> kPublicPaths = {"/login", "/signup", "/health", "/"};
    for (const auto& path : kPublicPaths) {
      if (req.path == path) {
        return std::nullopt;
      }
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
    return std::nullopt;
  }
};

}  // namespace kd
