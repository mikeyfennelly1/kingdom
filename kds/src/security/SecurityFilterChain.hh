#pragma once
#include "SecurityPredicate.hh"
#include <vector>
#include <string>

namespace kd {

/**
 * @brief Orchestrates a chain of security predicates.
 */
class SecurityFilterChain {
public:
    /**
     * @brief Construct a new Security Filter Chain.
     * 
     * @param predicateNames List of predicate names to include in the chain.
     */
    SecurityFilterChain(const std::vector<std::string>& predicateNames);

    /**
     * @brief Executes the validation chain on a message.
     * 
     * @param message The message to validate.
     * @return std::optional<SecurityError> The first error encountered, or std::nullopt if all pass.
     */
    std::optional<SecurityError> Execute(const Message& message);

private:
    std::vector<SecurityPredicatePtr> predicates_;
};

} // namespace kd
