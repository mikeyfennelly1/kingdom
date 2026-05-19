#pragma once
#include <string>

#include "SecurityPredicate.hh"

namespace kd {

class SecurityPredicateFactory {
 public:
  static auto GetPredicate(const std::string& predicateName) -> SecurityPredicatePtr;
};

}  // namespace kd
