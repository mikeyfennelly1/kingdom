#include "SecurityPredicateFactory.hh"

#include <stdexcept>

#include "../common/Constants.hh"
#include "SecurityPredicates.hh"

namespace kd {

auto SecurityPredicateFactory::GetPredicate(const std::string& predicateName)
    -> SecurityPredicatePtr {
  if (predicateName == security_predicates::ValidateSenderAuthenticity) {
    return std::make_unique<ValidateSenderAuthenticity>();
  }
  if (predicateName == security_predicates::ValidateUntampered) {
    return std::make_unique<ValidateUntampered>();
  }
  if (predicateName == security_predicates::ValidateAuthenticated) {
    return std::make_unique<ValidateAuthenticated>();
  }

  throw std::invalid_argument("Unknown security predicate: " + predicateName);
}

}  // namespace kd
