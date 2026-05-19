#pragma once
#include <spdlog/spdlog.h>

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
    return std::nullopt;  // Stub
  }
};

}  // namespace kd
