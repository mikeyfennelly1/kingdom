#include <gtest/gtest.h>
#include "../src/security/SecurityPredicateFactory.hh"
#include "../src/security/SecurityFilterChain.hh"
#include <kd/Message.hpp>

namespace kd {

class SecurityTest : public ::testing::Test {
protected:
    Message msg;
    
    void SetUp() override {
        msg.id = 1;
        msg.senderId = 100;
        msg.payload = "Test payload";
    }
};

TEST_F(SecurityTest, FactoryCreatesCorrectPredicates) {
    auto p1 = SecurityPredicateFactory::GetPredicate("ValidateSenderAuthenticity");
    ASSERT_NE(p1, nullptr);

    auto p2 = SecurityPredicateFactory::GetPredicate("ValidateUntampered");
    ASSERT_NE(p2, nullptr);

    auto p3 = SecurityPredicateFactory::GetPredicate("ValidateAuthenticated");
    ASSERT_NE(p3, nullptr);
}

TEST_F(SecurityTest, FactoryThrowsOnUnknownPredicate) {
    EXPECT_THROW(SecurityPredicateFactory::GetPredicate("UnknownPredicate"), std::invalid_argument);
}

TEST_F(SecurityTest, FilterChainExecutesAllSuccess) {
    std::vector<std::string> names = {
        "ValidateSenderAuthenticity",
        "ValidateUntampered",
        "ValidateAuthenticated"
    };
    
    SecurityFilterChain chain(names);
    auto result = chain.Execute(msg);
    
    EXPECT_FALSE(result.has_value());
}

// To test failure, we'd ideally have a way to inject a failing predicate.
// Since the factory is static and hardcoded, we'll stick to basic success for now
// until we refactor for better injectability if needed.

} // namespace kd
