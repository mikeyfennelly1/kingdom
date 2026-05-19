#pragma once
#include "SecurityPredicate.hh"
#include <string>
#include <memory>

namespace kd {

/**
 * @brief Factory for creating security predicates by name.
 */
class SecurityPredicateFactory {
public:
    /**
     * @brief Get a predicate instance by name.
     * 
     * @param predicateName The name of the predicate.
     * @return SecurityPredicatePtr A unique pointer to the predicate instance.
     */
    static SecurityPredicatePtr GetPredicate(const std::string& predicateName);
};

} // namespace kd
