#pragma once
#include <kd/Message.hpp>
#include "SecurityError.hh"
#include <optional>
#include <memory>

namespace kd {

/**
 * @brief Abstract base class for security validation steps.
 */
class SecurityPredicate {
public:
    virtual ~SecurityPredicate() = default;

    /**
     * @brief Validates a message against specific security criteria.
     * 
     * @param message The message to validate.
     * @return std::optional<SecurityError> Error details if validation fails, std::nullopt otherwise.
     */
    virtual std::optional<SecurityError> Validate(const Message& message) = 0;
};

using SecurityPredicatePtr = std::unique_ptr<SecurityPredicate>;

} // namespace kd
