#pragma once
#include <httplib.h>
#include <memory>
#include <optional>

#include "SecurityError.hh"

namespace kd {

class SecurityPredicate {
 public:
  virtual ~SecurityPredicate() = default;

  virtual auto Validate(const httplib::Request& req) -> std::optional<SecurityError> = 0;
};

using SecurityPredicatePtr = std::unique_ptr<SecurityPredicate>;

}  // namespace kd
