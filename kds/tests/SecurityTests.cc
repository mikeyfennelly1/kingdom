#include <gtest/gtest.h>
#include <httplib.h>
#include <sodium.h>

#include <array>
#include <kd/LocalKeyStore.hpp>
#include <string>

#include "../src/security/SecurityFilterChain.hh"
#include "../src/security/SecurityPredicateFactory.hh"

namespace kd {

class SecurityTest : public ::testing::Test {
 protected:
  httplib::Request req;

  void SetUp() override {
    req.method = "GET";
    req.path = "/health";
  }
};

TEST_F(SecurityTest, FactoryCreatesCorrectPredicates) {
  auto validateSenderAuthenticity =
      SecurityPredicateFactory::GetPredicate("ValidateSenderAuthenticity");
  ASSERT_NE(validateSenderAuthenticity, nullptr);

  auto validateUntampered = SecurityPredicateFactory::GetPredicate("ValidateUntampered");
  ASSERT_NE(validateUntampered, nullptr);

  auto validateAuthenticated = SecurityPredicateFactory::GetPredicate("ValidateAuthenticated");
  ASSERT_NE(validateAuthenticated, nullptr);
}

TEST_F(SecurityTest, FactoryThrowsOnUnknownPredicate) {
  EXPECT_THROW(SecurityPredicateFactory::GetPredicate("UnknownPredicate"), std::invalid_argument);
}

TEST_F(SecurityTest, FilterChainExecutesAllSuccess) {
  std::vector<std::string> names = {"ValidateSenderAuthenticity", "ValidateUntampered",
                                    "ValidateAuthenticated"};

  SecurityFilterChain chain(names);
  auto result = chain.Execute(req);

  EXPECT_FALSE(result.has_value());
}

namespace {

template <size_t N>
std::string encodeBase64(const std::array<unsigned char, N>& data) {
  std::string encoded(sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
  sodium_bin2base64(encoded.data(), encoded.size(), data.data(), data.size(),
                    sodium_base64_VARIANT_ORIGINAL);
  encoded.resize(encoded.find('\0'));
  return encoded;
}

}  // namespace

TEST(LocalKeyStoreTest, ContextBoundMessageRejectsWrongConversation) {
  ASSERT_GE(sodium_init(), 0);

  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> alicePublic{};
  std::array<unsigned char, crypto_box_SECRETKEYBYTES> alicePrivate{};
  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> bobPublic{};
  std::array<unsigned char, crypto_box_SECRETKEYBYTES> bobPrivate{};
  crypto_box_keypair(alicePublic.data(), alicePrivate.data());
  crypto_box_keypair(bobPublic.data(), bobPrivate.data());

  LocalIdentityKey alice{encodeBase64(alicePublic), alicePrivate};
  LocalIdentityKey bob{encodeBase64(bobPublic), bobPrivate};

  const auto payload = LocalKeyStore::encryptMessage("hello", alice, bob.publicKey, 7, 1, 2);

  EXPECT_EQ(LocalKeyStore::decryptMessage(payload, bob, alice.publicKey, 7, 1, 2), "hello");
  EXPECT_THROW(LocalKeyStore::decryptMessage(payload, bob, alice.publicKey, 8, 1, 2),
               std::runtime_error);
}

// To test failure, we'd ideally have a way to inject a failing predicate.
// Since the factory is static and hardcoded, we'll stick to basic success for now
// until we refactor for better injectability if needed.

}  // namespace kd
