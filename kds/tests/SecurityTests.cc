#include <gtest/gtest.h>
#include <httplib.h>
#include <sodium.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <kd/Conversation.hpp>
#include <kd/LocalKeyStore.hpp>
#include <kd/MessageStore.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "../src/common/Constants.hh"
#include "../src/security/SecurityFilterChain.hh"
#include "../src/security/SecurityPredicateFactory.hh"

// Test files use arbitrary numeric literals as IDs and timestamps.
// NOLINTBEGIN(readability-magic-numbers)

namespace kd {

class SecurityTest : public ::testing::Test {
 protected:
  httplib::Request req;

  void SetUp() override {
    req.method = "GET";
    req.path = routes::Health;
  }
};

TEST_F(SecurityTest, FactoryCreatesCorrectPredicates) {
  auto validateSenderAuthenticity =
      SecurityPredicateFactory::GetPredicate(security_predicates::ValidateSenderAuthenticity);
  ASSERT_NE(validateSenderAuthenticity, nullptr);

  auto validateUntampered =
      SecurityPredicateFactory::GetPredicate(security_predicates::ValidateUntampered);
  ASSERT_NE(validateUntampered, nullptr);

  auto validateAuthenticated =
      SecurityPredicateFactory::GetPredicate(security_predicates::ValidateAuthenticated);
  ASSERT_NE(validateAuthenticated, nullptr);
}

TEST_F(SecurityTest, FactoryThrowsOnUnknownPredicate) {
  EXPECT_THROW(SecurityPredicateFactory::GetPredicate("UnknownPredicate"), std::invalid_argument);
}

TEST_F(SecurityTest, FilterChainExecutesAllSuccess) {
  std::vector<std::string> names = {security_predicates::ValidateSenderAuthenticity,
                                    security_predicates::ValidateUntampered,
                                    security_predicates::ValidateAuthenticated};

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

std::vector<unsigned char> decodeBase64(const std::string& encoded) {
  std::vector<unsigned char> decoded(encoded.size(), 0);
  size_t decodedSize = 0;
  if (sodium_base642bin(decoded.data(), decoded.size(), encoded.c_str(), encoded.size(), nullptr,
                        &decodedSize, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    throw std::runtime_error("invalid base64");
  }
  decoded.resize(decodedSize);
  return decoded;
}

void appendUint64Le(std::vector<unsigned char>& out, uint64_t value) {
  constexpr size_t kBitsPerByte = 8;
  constexpr uint64_t kByteMask = 0xff;
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    out.push_back(static_cast<unsigned char>((value >> (i * kBitsPerByte)) & kByteMask));
  }
}

std::string signPreKey(uint64_t preKeyId, const std::string& publicKey,
                       const std::array<unsigned char, kSigningSecretKeySize>& signingSecretKey) {
  std::vector<unsigned char> input;
  const std::string info = "kd-x3dh-signed-prekey-signature-v1";
  input.insert(input.end(), info.begin(), info.end());
  appendUint64Le(input, preKeyId);
  auto publicKeyBytes = decodeBase64(publicKey);
  input.insert(input.end(), publicKeyBytes.begin(), publicKeyBytes.end());

  std::array<unsigned char, crypto_sign_BYTES> signature{};
  crypto_sign_detached(signature.data(), nullptr, input.data(), input.size(),
                       signingSecretKey.data());
  return encodeBase64(signature);
}

LocalPreKey makePreKey(uint64_t preKeyId) {
  LocalPreKey preKey;
  preKey.id = preKeyId;
  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> publicKey{};
  crypto_box_keypair(publicKey.data(), preKey.privateKey.data());
  preKey.publicKey = encodeBase64(publicKey);
  return preKey;
}

LocalIdentityKey makeIdentity() {
  LocalIdentityKey identity;
  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> publicKey{};
  crypto_box_keypair(publicKey.data(), identity.privateKey.data());
  identity.publicKey = encodeBase64(publicKey);

  std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> signingPublicKey{};
  crypto_sign_keypair(signingPublicKey.data(), identity.signingSecretKey.data());
  identity.signingPublicKey = encodeBase64(signingPublicKey);
  identity.signedPreKey = makePreKey(1);
  identity.oneTimePreKeys.push_back(makePreKey(11));
  return identity;
}

std::string bundleFor(const LocalIdentityKey& identity) {
  nlohmann::json oneTimePreKeys = nlohmann::json::array();
  for (const auto& preKey : identity.oneTimePreKeys) {
    oneTimePreKeys.push_back({{"id", preKey.id}, {"publicKey", preKey.publicKey}});
  }

  return nlohmann::json{{"version", 1},
                        {"algorithm", "KD-X3DH-BUNDLE-V1"},
                        {"identityKey", identity.publicKey},
                        {"signingKey", identity.signingPublicKey},
                        {"signedPreKey",
                         {{"id", identity.signedPreKey.id},
                          {"publicKey", identity.signedPreKey.publicKey},
                          {"signature",
                           signPreKey(identity.signedPreKey.id, identity.signedPreKey.publicKey,
                                      identity.signingSecretKey)}}},
                        {"oneTimePreKeys", oneTimePreKeys}}
      .dump();
}

}  // namespace

TEST(LocalKeyStoreTest, X3dhMessageDecryptsWithCorrectContext) {
  ASSERT_GE(sodium_init(), 0);

  auto alice = makeIdentity();
  auto bob = makeIdentity();
  const auto aliceBundle = bundleFor(alice);
  const auto bobBundle = bundleFor(bob);

  const auto payload = LocalKeyStore::encryptMessage("hello", alice, bobBundle, 7, 1, 2);

  EXPECT_EQ(LocalKeyStore::decryptMessage(payload, bob, aliceBundle, 7, 1, 2), "hello");
}

TEST(LocalKeyStoreTest, X3dhMessageRejectsWrongConversationContext) {
  ASSERT_GE(sodium_init(), 0);

  auto alice = makeIdentity();
  auto bob = makeIdentity();
  const auto aliceBundle = bundleFor(alice);
  const auto bobBundle = bundleFor(bob);

  const auto payload = LocalKeyStore::encryptMessage("hello", alice, bobBundle, 7, 1, 2);

  EXPECT_THROW(LocalKeyStore::decryptMessage(payload, bob, aliceBundle, 8, 1, 2),
               std::runtime_error);
}

TEST(LocalKeyStoreTest, X3dhMessageRejectsMissingUsedOneTimePreKey) {
  ASSERT_GE(sodium_init(), 0);

  auto alice = makeIdentity();
  auto bob = makeIdentity();
  const auto aliceBundle = bundleFor(alice);
  const auto bobBundle = bundleFor(bob);

  const auto payload = LocalKeyStore::encryptMessage("hello", alice, bobBundle, 7, 1, 2);

  EXPECT_EQ(LocalKeyStore::decryptMessage(payload, bob, aliceBundle, 7, 1, 2), "hello");
  EXPECT_THROW(LocalKeyStore::decryptMessage(payload, bob, aliceBundle, 7, 1, 2),
               std::runtime_error);
}

TEST(MessageStoreTest, FindBySenderReturnsOnlyMatchingMessages) {
  MessageStore store;
  store.add(Message{.id = 1,
                    .senderId = 10,
                    .conversationId = 100,
                    .payload = "msg1",
                    .timestamp = 1000,
                    .blockchainDigest = ""});
  store.add(Message{.id = 2,
                    .senderId = 20,
                    .conversationId = 100,
                    .payload = "msg2",
                    .timestamp = 2000,
                    .blockchainDigest = ""});
  store.add(Message{.id = 3,
                    .senderId = 10,
                    .conversationId = 100,
                    .payload = "msg3",
                    .timestamp = 3000,
                    .blockchainDigest = ""});

  auto results = store.findBySender(10);
  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0].id, 1U);
  EXPECT_EQ(results[1].id, 3U);
}

TEST(MessageStoreTest, FindBySenderReturnsEmptyForUnknownSender) {
  MessageStore store;
  store.add(Message{.id = 1,
                    .senderId = 10,
                    .conversationId = 100,
                    .payload = "msg1",
                    .timestamp = 1000,
                    .blockchainDigest = ""});

  auto results = store.findBySender(99);
  EXPECT_TRUE(results.empty());
}

TEST(ConversationTest, HasParticipantReturnsTrueForMember) {
  Conversation conv;
  conv.participantIds = {1, 2, 3};
  EXPECT_TRUE(conv.hasParticipant(2));
}

TEST(ConversationTest, HasParticipantReturnsFalseForNonMember) {
  Conversation conv;
  conv.participantIds = {1, 2, 3};
  EXPECT_FALSE(conv.hasParticipant(99));
}

}  // namespace kd

TEST(MessageStoreTest, SavesAndLoadsPlaintextByMessageId) {
  const auto path = std::filesystem::temp_directory_path() / "kingdom-message-store-test.json";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  kd::MessageStore writer(path);
  writer.savePlaintext(42, 7, 1, 123456, "cached hello");

  kd::MessageStore reader(path);
  ASSERT_EQ(reader.getPlaintext(42), std::optional<std::string>("cached hello"));
  EXPECT_EQ(reader.getPlaintext(43), std::nullopt);

  std::filesystem::remove(path, ignored);
}

TEST(MessageStoreTest, DeletePlaintextRemovesCachedMessage) {
  const auto path =
      std::filesystem::temp_directory_path() / "kingdom-message-store-delete-test.json";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  kd::MessageStore store(path);
  store.savePlaintext(42, 7, 1, 123456, "cached hello");
  ASSERT_EQ(store.getPlaintext(42), std::optional<std::string>("cached hello"));

  store.deletePlaintext(42);
  EXPECT_EQ(store.getPlaintext(42), std::nullopt);

  std::filesystem::remove(path, ignored);
}

TEST(MessageStoreTest, EncryptedStoreDoesNotPersistPlaintext) {
  ASSERT_GE(sodium_init(), 0);

  const auto path =
      std::filesystem::temp_directory_path() / "kingdom-message-store-encrypted-test.json";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  kd::MessageStore writer = kd::MessageStore::encryptedAtPath(path, "alice", "AlicePassword123");
  writer.savePlaintext(42, 7, 1, 123456, "cached secret hello");

  std::ifstream input(path);
  const std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_EQ(raw.find("cached secret hello"), std::string::npos);
  EXPECT_NE(raw.find("ciphertext"), std::string::npos);

  kd::MessageStore reader = kd::MessageStore::encryptedAtPath(path, "alice", "AlicePassword123");
  EXPECT_EQ(reader.getPlaintext(42), std::optional<std::string>("cached secret hello"));

  std::filesystem::remove(path, ignored);
}

TEST(MessageStoreTest, EncryptedStoreMigratesLegacyPlaintextStore) {
  ASSERT_GE(sodium_init(), 0);

  const auto path =
      std::filesystem::temp_directory_path() / "kingdom-message-store-migration-test.json";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  {
    std::ofstream output(path, std::ios::trunc);
    output << nlohmann::json{{"version", 1},
                             {"messages",
                              {{"42",
                                {{"messageId", 42},
                                 {"conversationId", 7},
                                 {"senderId", 1},
                                 {"timestamp", 123456},
                                 {"plaintext", "legacy secret"}}}}}}
                  .dump(2)
           << '\n';
  }

  kd::MessageStore store = kd::MessageStore::encryptedAtPath(path, "alice", "AlicePassword123");
  EXPECT_EQ(store.getPlaintext(42), std::optional<std::string>("legacy secret"));

  std::ifstream input(path);
  const std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_EQ(raw.find("legacy secret"), std::string::npos);
  EXPECT_NE(raw.find("ciphertext"), std::string::npos);

  std::filesystem::remove(path, ignored);
}

// NOLINTEND(readability-magic-numbers)
