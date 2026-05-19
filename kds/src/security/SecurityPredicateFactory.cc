#include "SecurityPredicateFactory.hh"
#include "SecurityPredicates.hh"
#include <stdexcept>

namespace kd {

SecurityPredicatePtr SecurityPredicateFactory::GetPredicate(const std::string& predicateName) {
    if (predicateName == "ValidateSenderAuthenticity") {
        return std::make_unique<ValidateSenderAuthenticity>();
    } else if (predicateName == "ValidateUntampered") {
        return std::make_unique<ValidateUntampered>();
    } else if (predicateName == "ValidateAuthenticated") {
        return std::make_unique<ValidateAuthenticated>();
    }
    
    throw std::invalid_argument("Unknown security predicate: " + predicateName);
}

} // namespace kd
