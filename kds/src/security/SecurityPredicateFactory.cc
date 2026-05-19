#include "SecurityPredicateFactory.hh"

#include <stdexcept>

#include "SecurityPredicates.hh"

namespace kd {

auto SecurityPredicateFactory::GetPredicate(const std::string& predicateName)
    -> SecurityPredicatePtr {
  if (predicateName == "ValidateSenderAuthenticity") {
    return std::make_unique<ValidateSenderAuthenticity>();
  }
  if (predicateName == "ValidateUntampered") {
    return std::make_unique<ValidateUntampered>();
  }
  if (predicateName == "ValidateAuthenticated") {
    return std::make_unique<ValidateAuthenticated>();
  }

  throw std::invalid_argument("Unknown security predicate: " + predicateName);
}

}  // namespace kd
