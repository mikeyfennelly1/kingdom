#pragma once
#include <string>

namespace kd {

struct SecurityError {
    std::string message;
    int httpStatusCode;
};

}  // namespace kd
