#include "SecurityFilterChain.hh"

#include "SecurityPredicateFactory.hh"

namespace kd {

SecurityFilterChain::SecurityFilterChain(const std::vector<std::string>& predicateNames) {
  for (const auto& name : predicateNames) {
    predicates_.push_back(SecurityPredicateFactory::GetPredicate(name));
  }
}

auto SecurityFilterChain::Execute(const httplib::Request& req) -> std::optional<SecurityError> {
  for (const auto& predicate : predicates_) {
    auto result = predicate->Validate(req);
    if (result.has_value()) {
      return result;
    }
  }
  return std::nullopt;
}

}  // namespace kd
