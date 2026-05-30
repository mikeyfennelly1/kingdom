#pragma once
#include <httplib.h>

#include <string>
#include <vector>

#include "SecurityPredicate.hh"

namespace kd {

/**
 * @brief Orchestrates a chain of security predicates.
 */
class SecurityFilterChain {
 public:
  SecurityFilterChain(const std::vector<std::string>& predicateNames);

  auto Execute(const httplib::Request& req) -> std::optional<SecurityError>;

 private:
  std::vector<SecurityPredicatePtr> predicates_;
};

}  // namespace kd
