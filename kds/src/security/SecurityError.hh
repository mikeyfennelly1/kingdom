#pragma once
#include <string>

namespace kd {

/**
 * @brief Represents a security validation error.
 */
struct SecurityError {
    std::string message;
    int httpStatusCode;
};

} // namespace kd
