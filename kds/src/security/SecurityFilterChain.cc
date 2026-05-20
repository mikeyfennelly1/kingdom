#include "SecurityFilterChain.hh"

#include <spdlog/spdlog.h>

#include "SecurityPredicateFactory.hh"

namespace kd {

SecurityFilterChain::SecurityFilterChain(const std::vector<std::string>& predicateNames) {
    for (const auto& name : predicateNames) {
        predicates_.push_back(SecurityPredicateFactory::GetPredicate(name));
    }
}

auto SecurityFilterChain::Execute(const httplib::Request& req) -> std::optional<SecurityError> {
    spdlog::debug("SecurityFilterChain: Executing chain for {} {}", req.method, req.path);
    for (const auto& predicate : predicates_) {
        auto result = predicate->Validate(req);
        if (result.has_value()) {
            spdlog::debug("SecurityFilterChain: Predicate failed: {}", result->message);
            return result;
        }
    }
    spdlog::debug("SecurityFilterChain: All predicates passed");
    return std::nullopt;
}

}  // namespace kd
