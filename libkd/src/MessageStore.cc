#include "kd/MessageStore.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace kd {
namespace {

constexpr unsigned long long kCacheKdfOpsLimit = crypto_pwhash_OPSLIMIT_MODERATE;
constexpr size_t kCacheKdfMemLimit = crypto_pwhash_MEMLIMIT_MODERATE;
constexpr const char* kCacheKdfAlgorithm = "Argon2id+HKDF-SHA256";
constexpr const char* kCacheEncryptionAlgorithm = "XChaCha20-Poly1305";
constexpr const char* kCacheEncryptionInfo = "kd-message-cache-encryption-v1";
constexpr const char* kCacheAssociatedDataPrefix = "kingdom-message-cache-v2";

void ensureSodiumInitialized() {
  if (sodium_init() < 0) {
    throw std::runtime_error("Failed to initialize libsodium");
  }
}

std::string base64Encode(const unsigned char* data, size_t size) {
  std::string encoded(sodium_base64_ENCODED_LEN(size, sodium_base64_VARIANT_ORIGINAL), '\0');
  sodium_bin2base64(encoded.data(), encoded.size(), data, size, sodium_base64_VARIANT_ORIGINAL);
  encoded.resize(encoded.find('\0'));
  return encoded;
}

template <size_t N>
std::string base64Encode(const std::array<unsigned char, N>& data) {
  return base64Encode(data.data(), data.size());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<unsigned char> base64Decode(const std::string& encoded, const std::string& fieldName) {
  std::vector<unsigned char> decoded(encoded.size(), 0);
  size_t decodedSize = 0;
  if (sodium_base642bin(decoded.data(), decoded.size(), encoded.c_str(), encoded.size(), nullptr,
                        &decodedSize, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    throw std::runtime_error("Invalid base64 field: " + fieldName);
  }
  decoded.resize(decodedSize);
  return decoded;
}

template <size_t N>
std::array<unsigned char, N> base64DecodeArray(const std::string& encoded,
                                               const std::string& fieldName) {
  auto decoded = base64Decode(encoded, fieldName);
  if (decoded.size() != N) {
    throw std::runtime_error("Unexpected decoded size for field: " + fieldName);
  }

  std::array<unsigned char, N> value{};
  std::copy(decoded.begin(), decoded.end(), value.begin());
  return value;
}

std::array<unsigned char, MessageStore::kEncryptionKeySize> deriveKey(const unsigned char* secret,
                                                                      size_t secretSize,
                                                                      std::string_view info,
                                                                      const unsigned char* salt,
                                                                      size_t saltSize) {
  std::array<unsigned char, MessageStore::kEncryptionKeySize> derivedKey{};

  EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
  if (kdf == nullptr) {
    throw std::runtime_error("Failed to fetch HKDF implementation");
  }

  EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);
  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create HKDF context");
  }

  char digestName[] = "SHA256";  // NOLINT(modernize-avoid-c-arrays)
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digestName, 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<unsigned char*>(secret),
                                        secretSize),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<unsigned char*>(salt),
                                        saltSize),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<char*>(info.data()),
                                        info.size()),
      OSSL_PARAM_construct_end()};

  const int result = EVP_KDF_derive(ctx, derivedKey.data(), derivedKey.size(), params);
  EVP_KDF_CTX_free(ctx);
  if (result != 1) {
    sodium_memzero(derivedKey.data(), derivedKey.size());
    throw std::runtime_error("Failed to derive HKDF key");
  }

  return derivedKey;
}

std::array<unsigned char, MessageStore::kEncryptionKeySize> deriveCacheEncryptionKey(
    const std::string& password, const std::array<unsigned char, MessageStore::kSaltSize>& salt,
    unsigned long long opsLimit, size_t memLimit) {
  std::array<unsigned char, MessageStore::kEncryptionKeySize> argon2Secret{};
  if (crypto_pwhash(argon2Secret.data(), argon2Secret.size(), password.c_str(), password.size(),
                    salt.data(), opsLimit, memLimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive message-cache encryption secret");
  }

  auto key = deriveKey(argon2Secret.data(), argon2Secret.size(), kCacheEncryptionInfo, salt.data(),
                       salt.size());
  sodium_memzero(argon2Secret.data(), argon2Secret.size());
  return key;
}

std::string sanitizedUsername(const std::string& username) {
  std::string value;
  value.reserve(username.size());
  for (char chr : username) {
    if ((chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z') || (chr >= '0' && chr <= '9') ||
        chr == '-' || chr == '_') {
      value.push_back(chr);
    } else {
      value.push_back('_');
    }
  }
  return value.empty() ? "user" : value;
}

std::filesystem::path defaultStorePath(const std::string& username) {
  const char* home = std::getenv("HOME");
  if (home == nullptr || std::string(home).empty()) {
    throw std::runtime_error("HOME environment variable not set");
  }
  return std::filesystem::path(home) / ".kingdom" / "messages" /
         (sanitizedUsername(username) + ".json");
}

nlohmann::json emptyStore() {
  return nlohmann::json{{"version", 1}, {"messages", nlohmann::json::object()}};
}

nlohmann::json parseJsonFileLenient(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return emptyStore();
  }

  auto parsed = nlohmann::json::parse(input, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return emptyStore();
  }
  return parsed;
}

nlohmann::json parseJsonFileStrict(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open local message store for reading: " + path.string());
  }

  auto parsed = nlohmann::json::parse(input, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    throw std::runtime_error("Local message store is not valid JSON: " + path.string());
  }
  return parsed;
}

void normalizeStore(nlohmann::json& store) {
  if (!store.contains("messages") || !store["messages"].is_object()) {
    store["messages"] = nlohmann::json::object();
  }
}

void writeJsonFile(const std::filesystem::path& path, const nlohmann::json& body) {
  if (path.empty()) {
    return;
  }

  std::filesystem::create_directories(path.parent_path());
  const auto tmpPath = path.string() + ".tmp";
  {
    std::ofstream output(tmpPath, std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Failed to open local message store for writing: " + path.string());
    }
    output << body.dump(2) << '\n';
    if (!output) {
      throw std::runtime_error("Failed to write local message store: " + path.string());
    }
  }

  std::filesystem::rename(tmpPath, path);
  std::error_code ignored;
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ignored);
}

std::string cacheAssociatedData(const std::string& username) {
  std::string aad{kCacheAssociatedDataPrefix};
  aad.push_back('\0');
  aad += username;
  return aad;
}

struct StoreKdfParams {
  std::array<unsigned char, MessageStore::kSaltSize> salt{};
  unsigned long long opsLimit = kCacheKdfOpsLimit;
  size_t memLimit = kCacheKdfMemLimit;
};

StoreKdfParams kdfParamsForStore(const std::filesystem::path& path) {
  StoreKdfParams params;
  randombytes_buf(params.salt.data(), params.salt.size());

  if (!std::filesystem::exists(path)) {
    return params;
  }

  auto parsed = parseJsonFileLenient(path);
  if (parsed.value("version", 1) != 2 || !parsed.contains("kdf") ||
      !parsed["kdf"].contains("salt") || !parsed["kdf"]["salt"].is_string()) {
    return params;
  }

  const auto& kdf = parsed["kdf"];
  params.salt =
      base64DecodeArray<MessageStore::kSaltSize>(kdf["salt"].get<std::string>(), "kdf.salt");
  params.opsLimit = kdf.value("opsLimit", kCacheKdfOpsLimit);
  params.memLimit = kdf.value("memLimit", kCacheKdfMemLimit);
  return params;
}

}  // namespace

MessageStore::MessageStore(const std::string& username) : storePath_(defaultStorePath(username)) {
}

MessageStore::MessageStore(std::filesystem::path storePath) : storePath_(std::move(storePath)) {
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
MessageStore::MessageStore(std::filesystem::path storePath, std::string username,
                           std::array<unsigned char, kEncryptionKeySize> encryptionKey,
                           std::array<unsigned char, kSaltSize> salt, unsigned long long opsLimit,
                           size_t memLimit)
    : storePath_(std::move(storePath))
    , username_(std::move(username))
    , encryptionKey_(encryptionKey)
    , salt_(salt)
    , opsLimit_(opsLimit)
    , memLimit_(memLimit)
    , encryptedAtRest_(true) {
}
// NOLINTEND(bugprone-easily-swappable-parameters)

MessageStore MessageStore::encryptedForUser(const std::string& username,
                                            const std::string& password) {
  return encryptedAtPath(defaultStorePath(username), username, password);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
MessageStore MessageStore::encryptedAtPath(std::filesystem::path storePath,
                                           const std::string& username,
                                           const std::string& password) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  ensureSodiumInitialized();

  auto params = kdfParamsForStore(storePath);
  auto key = deriveCacheEncryptionKey(password, params.salt, params.opsLimit, params.memLimit);

  MessageStore store(std::move(storePath), username, key, params.salt, params.opsLimit,
                     params.memLimit);
  sodium_memzero(key.data(), key.size());

  if (std::filesystem::exists(store.storePath_)) {
    auto parsed = parseJsonFileLenient(store.storePath_);
    if (parsed.value("version", 1) == 1) {
      normalizeStore(parsed);
      store.writeStore_(parsed);
    }
  }

  return store;
}

void MessageStore::add(Message message) {
  messages_.push_back(std::move(message));
}

const std::vector<Message>& MessageStore::getAll() const {
  return messages_;
}

std::vector<Message> MessageStore::findBySender(uint64_t senderId) const {
  std::vector<Message> result;
  std::ranges::copy_if(messages_, std::back_inserter(result),
                       [senderId](const Message& message) { return message.senderId == senderId; });
  return result;
}

void MessageStore::clear() {
  messages_.clear();
}

void MessageStore::cachePublicKey(uint64_t userId, const std::string& publicKey) {
  publicKeyCache_[userId] = publicKey;
}

std::optional<std::string> MessageStore::getCachedPublicKey(uint64_t userId) const {
  auto iter = publicKeyCache_.find(userId);
  if (iter == publicKeyCache_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

nlohmann::json MessageStore::readStore_() const {
  if (storePath_.empty() || !std::filesystem::exists(storePath_)) {
    return emptyStore();
  }

  if (!encryptedAtRest_) {
    auto parsed = parseJsonFileLenient(storePath_);
    normalizeStore(parsed);
    return parsed;
  }

  auto envelope = parseJsonFileStrict(storePath_);
  if (envelope.value("version", 1) == 1) {
    normalizeStore(envelope);
    return envelope;
  }

  if (envelope.value("version", 0) != 2 ||
      envelope.value("algorithm", "") != kCacheEncryptionAlgorithm || !envelope.contains("kdf") ||
      !envelope.contains("nonce") || !envelope.contains("ciphertext")) {
    throw std::runtime_error("Unsupported local message store format: " + storePath_.string());
  }

  const auto& kdf = envelope.at("kdf");
  if (kdf.value("algorithm", "") != kCacheKdfAlgorithm || !kdf.contains("hkdf") ||
      kdf.at("hkdf").value("digest", "") != "SHA-256" ||
      kdf.at("hkdf").value("info", "") != kCacheEncryptionInfo) {
    throw std::runtime_error("Unsupported local message store crypto parameters: " +
                             storePath_.string());
  }

  const auto nonce =
      base64DecodeArray<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(envelope.at("nonce"),
                                                                      "nonce");
  const auto ciphertext = base64Decode(envelope.at("ciphertext"), "ciphertext");
  if (ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
    throw std::runtime_error("Local message store ciphertext is too short: " + storePath_.string());
  }

  const auto aad = cacheAssociatedData(username_);
  std::vector<unsigned char> plaintext(ciphertext.size() -
                                       crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long plaintextSize = 0;
  const auto decryptResult =
      crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &plaintextSize, nullptr,
                                                 ciphertext.data(), ciphertext.size(),
                                                 reinterpret_cast<const unsigned char*>(aad.data()),
                                                 aad.size(), nonce.data(), encryptionKey_.data());
  if (decryptResult != 0) {
    throw std::runtime_error("Failed to decrypt local message store: " + storePath_.string());
  }
  plaintext.resize(plaintextSize);

  auto parsed = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(plaintext.data()),
                                                  plaintext.size()),
                                      nullptr, false);
  sodium_memzero(plaintext.data(), plaintext.size());
  if (parsed.is_discarded() || !parsed.is_object()) {
    throw std::runtime_error("Decrypted local message store is not valid JSON: " +
                             storePath_.string());
  }
  normalizeStore(parsed);
  return parsed;
}

void MessageStore::writeStore_(const nlohmann::json& store) const {
  if (!encryptedAtRest_) {
    writeJsonFile(storePath_, store);
    return;
  }

  ensureSodiumInitialized();

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  const auto plaintextText = store.dump(2);
  std::vector<unsigned char> ciphertext(plaintextText.size() +
                                        crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertextSize = 0;
  const auto aad = cacheAssociatedData(username_);
  if (crypto_aead_xchacha20poly1305_ietf_encrypt(
          ciphertext.data(), &ciphertextSize,
          reinterpret_cast<const unsigned char*>(plaintextText.data()), plaintextText.size(),
          reinterpret_cast<const unsigned char*>(aad.data()), aad.size(), nullptr, nonce.data(),
          encryptionKey_.data()) != 0) {
    throw std::runtime_error("Failed to encrypt local message store: " + storePath_.string());
  }
  ciphertext.resize(ciphertextSize);

  nlohmann::json envelope = {{"version", 2},
                             {"algorithm", kCacheEncryptionAlgorithm},
                             {"kdf",
                              {{"algorithm", kCacheKdfAlgorithm},
                               {"hkdf", {{"digest", "SHA-256"}, {"info", kCacheEncryptionInfo}}},
                               {"salt", base64Encode(salt_)},
                               {"opsLimit", opsLimit_},
                               {"memLimit", memLimit_}}},
                             {"nonce", base64Encode(nonce)},
                             {"ciphertext", base64Encode(ciphertext.data(), ciphertext.size())}};

  writeJsonFile(storePath_, envelope);
}

std::optional<std::string> MessageStore::getPlaintext(uint64_t messageId) const {
  const auto store = readStore_();
  const auto key = std::to_string(messageId);
  if (!store["messages"].contains(key)) {
    return std::nullopt;
  }

  const auto& record = store["messages"][key];
  if (!record.is_object() || !record.contains("plaintext") || !record["plaintext"].is_string()) {
    return std::nullopt;
  }
  return record["plaintext"].get<std::string>();
}

void MessageStore::savePlaintext(uint64_t messageId, uint64_t conversationId, uint64_t senderId,
                                 uint64_t timestamp, const std::string& plaintext) const {
  auto store = readStore_();
  store["messages"][std::to_string(messageId)] = {{"messageId", messageId},
                                                  {"conversationId", conversationId},
                                                  {"senderId", senderId},
                                                  {"timestamp", timestamp},
                                                  {"plaintext", plaintext}};
  writeStore_(store);
}

void MessageStore::deletePlaintext(uint64_t messageId) const {
  auto store = readStore_();
  store["messages"].erase(std::to_string(messageId));
  writeStore_(store);
}

}  // namespace kd
