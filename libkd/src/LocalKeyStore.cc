#include "kd/LocalKeyStore.hpp"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace kd {
namespace {

constexpr unsigned long long kKdfOpsLimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr size_t kKdfMemLimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;
constexpr const char* kKdfAlgorithm = "Argon2id";
constexpr const char* kKdfHkdfAlgorithm = "Argon2id+HKDF-SHA256";
constexpr const char* kKeyEncryptionInfo = "kd-key-encryption-v1";
constexpr const char* kMessageMacInfo = "kd-message-context-mac-v1";
constexpr const char* kMessageMacSalt = "kd-message-context-mac-salt-v1";
constexpr std::array<unsigned char, 4> kMessagePayloadMagic{'K', 'D', 'M', '1'};

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

std::vector<unsigned char> base64Decode(const std::string& encoded, const std::string& fieldName) {
  std::vector<unsigned char> decoded(encoded.size(), 0);
  size_t decodedSize = 0;
  if (sodium_base642bin(decoded.data(), decoded.size(), encoded.c_str(), encoded.size(), nullptr,
                        &decodedSize, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
    throw std::runtime_error("Invalid base64 in local key file field: " + fieldName);
  }
  decoded.resize(decodedSize);
  return decoded;
}

template <size_t N>
std::array<unsigned char, N> base64DecodeArray(const std::string& encoded,
                                               const std::string& fieldName) {
  auto decoded = base64Decode(encoded, fieldName);
  if (decoded.size() != N) {
    throw std::runtime_error("Unexpected decoded size for local key file field: " + fieldName);
  }

  std::array<unsigned char, N> value{};
  std::copy(decoded.begin(), decoded.end(), value.begin());
  return value;
}

nlohmann::json readJsonFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open local key file for reading: " + path.string());
  }

  auto body = nlohmann::json::parse(input, nullptr, false);
  if (body.is_discarded()) {
    throw std::runtime_error("Local key file is not valid JSON: " + path.string());
  }
  return body;
}

void writeJsonFile(const std::filesystem::path& path, const nlohmann::json& body) {
  std::filesystem::create_directories(path.parent_path());

  std::ofstream output(path, std::ios::out | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Failed to open local key file for writing: " + path.string());
  }

  output << body.dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("Failed to write local key file: " + path.string());
  }

  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
}

std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> deriveKey(
    const std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>& sharedSecret,
    const std::string& info, const std::array<unsigned char, crypto_pwhash_SALTBYTES>& salt) {
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> derivedKey{};

  EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
  if (kdf == nullptr) {
    throw std::runtime_error("Failed to fetch HKDF implementation");
  }

  EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);
  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create HKDF context");
  }

  char digestName[] = "SHA256";
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digestName, 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                        const_cast<unsigned char*>(sharedSecret.data()),
                                        sharedSecret.size()),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                        const_cast<unsigned char*>(salt.data()), salt.size()),
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

std::array<unsigned char, crypto_auth_KEYBYTES> deriveMessageMacKey(
    const unsigned char* sharedSecret, size_t sharedSecretSize) {
  static_assert(crypto_auth_KEYBYTES == 32);
  std::array<unsigned char, crypto_auth_KEYBYTES> macKey{};

  EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
  if (kdf == nullptr) {
    throw std::runtime_error("Failed to fetch HKDF implementation");
  }

  EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);
  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create HKDF context");
  }

  char digestName[] = "SHA256";
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digestName, 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                        const_cast<unsigned char*>(sharedSecret), sharedSecretSize),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(kMessageMacSalt),
                                        std::char_traits<char>::length(kMessageMacSalt)),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<char*>(kMessageMacInfo),
                                        std::char_traits<char>::length(kMessageMacInfo)),
      OSSL_PARAM_construct_end()};

  const int result = EVP_KDF_derive(ctx, macKey.data(), macKey.size(), params);
  EVP_KDF_CTX_free(ctx);
  if (result != 1) {
    sodium_memzero(macKey.data(), macKey.size());
    throw std::runtime_error("Failed to derive message MAC key");
  }

  return macKey;
}

std::array<unsigned char, crypto_auth_KEYBYTES> deriveMessageMacKey(
    const std::vector<unsigned char>& peerPublicKey,
    const std::array<unsigned char, kPrivateKeySize>& privateKey) {
  std::array<unsigned char, crypto_box_BEFORENMBYTES> sharedSecret{};
  if (crypto_box_beforenm(sharedSecret.data(), peerPublicKey.data(), privateKey.data()) != 0) {
    throw std::runtime_error("Failed to derive message shared secret");
  }

  auto macKey = deriveMessageMacKey(sharedSecret.data(), sharedSecret.size());
  sodium_memzero(sharedSecret.data(), sharedSecret.size());
  return macKey;
}

void appendUint64Le(std::vector<unsigned char>& out, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) {
    out.push_back(static_cast<unsigned char>((value >> (i * 8)) & 0xff));
  }
}

std::vector<unsigned char> buildContextMacInput(
    uint64_t conversationId, uint64_t senderId, uint64_t recipientId,
    const std::array<unsigned char, crypto_box_NONCEBYTES>& nonce,
    const std::vector<unsigned char>& ciphertext) {
  std::vector<unsigned char> input;
  input.reserve(24 + nonce.size() + ciphertext.size());
  appendUint64Le(input, conversationId);
  appendUint64Le(input, senderId);
  appendUint64Le(input, recipientId);
  input.insert(input.end(), nonce.begin(), nonce.end());
  input.insert(input.end(), ciphertext.begin(), ciphertext.end());
  return input;
}

std::string sanitizedUsername(const std::string& username) {
  std::string value;
  value.reserve(username.size());
  for (char ch : username) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_') {
      value.push_back(ch);
    } else {
      value.push_back('_');
    }
  }
  return value.empty() ? "user" : value;
}

}  // namespace

RegistrationKeyMaterial LocalKeyStore::createForSignup(const std::string& username,
                                                       const std::string& password) {
  ensureSodiumInitialized();

  auto keyPath = defaultKeyPath_(username);
  if (std::filesystem::exists(keyPath)) {
    throw std::runtime_error("Local key file already exists: " + keyPath.string());
  }

  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> publicKey{};
  std::array<unsigned char, crypto_box_SECRETKEYBYTES> privateKey{};
  crypto_box_keypair(publicKey.data(), privateKey.data());

  std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
  randombytes_buf(salt.data(), salt.size());

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> argon2Secret{};
  if (crypto_pwhash(argon2Secret.data(), argon2Secret.size(), password.c_str(), password.size(),
                    salt.data(), kKdfOpsLimit, kKdfMemLimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive local key-encryption secret");
  }
  auto keyEncryptionKey = deriveKey(argon2Secret, kKeyEncryptionInfo, salt);
  sodium_memzero(argon2Secret.data(), argon2Secret.size());

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  std::vector<unsigned char> ciphertext(privateKey.size() +
                                        crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertextSize = 0;
  const std::string associatedData = username;
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      ciphertext.data(), &ciphertextSize, privateKey.data(), privateKey.size(),
      reinterpret_cast<const unsigned char*>(associatedData.data()), associatedData.size(), nullptr,
      nonce.data(), keyEncryptionKey.data());
  ciphertext.resize(ciphertextSize);

  sodium_memzero(privateKey.data(), privateKey.size());
  sodium_memzero(keyEncryptionKey.data(), keyEncryptionKey.size());

  auto publicKeyEncoded = base64Encode(publicKey);
  nlohmann::json file = {{"version", 1},
                         {"username", username},
                         {"publicKey", publicKeyEncoded},
                         {"privateKey",
                          {{"algorithm", "XChaCha20-Poly1305"},
                           {"ciphertext", base64Encode(ciphertext.data(), ciphertext.size())},
                           {"nonce", base64Encode(nonce)}}},
                         {"kdf",
                          {{"algorithm", kKdfHkdfAlgorithm},
                           {"hkdf",
                            nlohmann::json{{"digest", "SHA-256"}, {"info", kKeyEncryptionInfo}}},
                           {"salt", base64Encode(salt)},
                           {"opsLimit", kKdfOpsLimit},
                           {"memLimit", kKdfMemLimit}}}};

  writeJsonFile(keyPath, file);

  return RegistrationKeyMaterial{publicKeyEncoded, keyPath.string()};
}

LocalIdentityKey LocalKeyStore::loadForLogin(const std::string& username,
                                             const std::string& password) {
  ensureSodiumInitialized();

  auto keyPath = defaultKeyPath_(username);
  auto file = readJsonFile(keyPath);
  if (!file.contains("version") || !file.contains("username") || !file.contains("publicKey") ||
      !file.contains("privateKey") || !file.contains("kdf")) {
    throw std::runtime_error("Local key file is missing required fields: " + keyPath.string());
  }
  if (file["version"] != 1 || file["username"].get<std::string>() != username) {
    throw std::runtime_error("Local key file does not match this user: " + keyPath.string());
  }

  const auto& privateKeyJson = file["privateKey"];
  const auto& kdfJson = file["kdf"];
  if (!privateKeyJson.contains("algorithm") || !privateKeyJson.contains("ciphertext") ||
      !privateKeyJson.contains("nonce") || !kdfJson.contains("algorithm") ||
      !kdfJson.contains("salt") || !kdfJson.contains("opsLimit") || !kdfJson.contains("memLimit")) {
    throw std::runtime_error("Local key file is missing encryption parameters: " +
                             keyPath.string());
  }
  const auto privateKeyAlgorithm = privateKeyJson["algorithm"].get<std::string>();
  const auto kdfAlgorithm = kdfJson["algorithm"].get<std::string>();
  if (privateKeyAlgorithm != "XChaCha20-Poly1305" ||
      (kdfAlgorithm != kKdfAlgorithm && kdfAlgorithm != kKdfHkdfAlgorithm)) {
    throw std::runtime_error("Unsupported local key file crypto parameters: " + keyPath.string());
  }
  if (kdfAlgorithm == kKdfHkdfAlgorithm) {
    if (!kdfJson.contains("hkdf") || !kdfJson["hkdf"].contains("digest") ||
        !kdfJson["hkdf"].contains("info") ||
        kdfJson["hkdf"]["digest"].get<std::string>() != "SHA-256" ||
        kdfJson["hkdf"]["info"].get<std::string>() != kKeyEncryptionInfo) {
      throw std::runtime_error("Unsupported HKDF parameters in local key file: " +
                               keyPath.string());
    }
  }

  auto salt =
      base64DecodeArray<crypto_pwhash_SALTBYTES>(kdfJson["salt"].get<std::string>(), "kdf.salt");
  auto nonce = base64DecodeArray<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(
      privateKeyJson["nonce"].get<std::string>(), "privateKey.nonce");
  auto ciphertext =
      base64Decode(privateKeyJson["ciphertext"].get<std::string>(), "privateKey.ciphertext");
  auto opsLimit = kdfJson["opsLimit"].get<unsigned long long>();
  auto memLimit = kdfJson["memLimit"].get<size_t>();

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> argon2Secret{};
  if (crypto_pwhash(argon2Secret.data(), argon2Secret.size(), password.c_str(), password.size(),
                    salt.data(), opsLimit, memLimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive local key-encryption secret");
  }

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> keyEncryptionKey{};
  if (kdfAlgorithm == kKdfHkdfAlgorithm) {
    keyEncryptionKey = deriveKey(argon2Secret, kKeyEncryptionInfo, salt);
  } else {
    keyEncryptionKey = argon2Secret;
  }
  sodium_memzero(argon2Secret.data(), argon2Secret.size());

  LocalIdentityKey identity{file["publicKey"].get<std::string>(), {}};
  unsigned long long privateKeySize = 0;
  const std::string associatedData = username;
  auto decryptResult = crypto_aead_xchacha20poly1305_ietf_decrypt(
      identity.privateKey.data(), &privateKeySize, nullptr, ciphertext.data(), ciphertext.size(),
      reinterpret_cast<const unsigned char*>(associatedData.data()), associatedData.size(),
      nonce.data(), keyEncryptionKey.data());
  sodium_memzero(keyEncryptionKey.data(), keyEncryptionKey.size());

  if (decryptResult != 0 || privateKeySize != identity.privateKey.size()) {
    sodium_memzero(identity.privateKey.data(), identity.privateKey.size());
    throw std::runtime_error("Failed to decrypt local private key for user: " + username);
  }

  return identity;
}

std::string LocalKeyStore::encryptMessage(const std::string& plaintext,
                                          const LocalIdentityKey& sender,
                                          const std::string& recipientPkB64,
                                          uint64_t conversationId, uint64_t senderId,
                                          uint64_t recipientId) {
  ensureSodiumInitialized();

  auto recipientPk = base64Decode(recipientPkB64, "recipientPublicKey");
  if (recipientPk.size() != crypto_box_PUBLICKEYBYTES) {
    throw std::runtime_error("Invalid recipient public key size");
  }

  std::array<unsigned char, crypto_box_NONCEBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  std::vector<unsigned char> ciphertext(plaintext.size() + crypto_box_MACBYTES);
  if (crypto_box_easy(ciphertext.data(), reinterpret_cast<const unsigned char*>(plaintext.data()),
                      static_cast<unsigned long long>(plaintext.size()), nonce.data(),
                      recipientPk.data(), sender.privateKey.data()) != 0) {
    throw std::runtime_error("Encryption failed");
  }

  auto macKey = deriveMessageMacKey(recipientPk, sender.privateKey);
  auto macInput = buildContextMacInput(conversationId, senderId, recipientId, nonce, ciphertext);

  std::array<unsigned char, crypto_auth_BYTES> mac{};
  if (crypto_auth(mac.data(), macInput.data(), static_cast<unsigned long long>(macInput.size()),
                  macKey.data()) != 0) {
    sodium_memzero(macKey.data(), macKey.size());
    throw std::runtime_error("Failed to authenticate message context");
  }
  sodium_memzero(macKey.data(), macKey.size());

  std::vector<unsigned char> combined;
  combined.reserve(kMessagePayloadMagic.size() + mac.size() + nonce.size() + ciphertext.size());
  combined.insert(combined.end(), kMessagePayloadMagic.begin(), kMessagePayloadMagic.end());
  combined.insert(combined.end(), mac.begin(), mac.end());
  combined.insert(combined.end(), nonce.begin(), nonce.end());
  combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
  return base64Encode(combined.data(), combined.size());
}

std::string LocalKeyStore::decryptMessage(const std::string& payloadB64,
                                          const LocalIdentityKey& recipient,
                                          const std::string& senderPkB64, uint64_t conversationId,
                                          uint64_t senderId, uint64_t recipientId) {
  ensureSodiumInitialized();

  auto combined = base64Decode(payloadB64, "payload");
  constexpr size_t kHeaderSize = 4 + crypto_auth_BYTES + crypto_box_NONCEBYTES;
  if (combined.size() < kHeaderSize + crypto_box_MACBYTES) {
    throw std::runtime_error("Payload too short to be valid context-bound ciphertext");
  }
  if (!std::equal(kMessagePayloadMagic.begin(), kMessagePayloadMagic.end(), combined.begin())) {
    throw std::runtime_error("Unsupported legacy or unknown message payload format");
  }

  size_t offset = kMessagePayloadMagic.size();
  std::array<unsigned char, crypto_auth_BYTES> mac{};
  std::copy(combined.begin() + offset, combined.begin() + offset + mac.size(), mac.begin());
  offset += mac.size();

  std::array<unsigned char, crypto_box_NONCEBYTES> nonce{};
  std::copy(combined.begin() + offset, combined.begin() + offset + nonce.size(), nonce.begin());
  offset += nonce.size();

  std::vector<unsigned char> ciphertext(combined.begin() + offset, combined.end());

  auto senderPk = base64Decode(senderPkB64, "senderPublicKey");
  if (senderPk.size() != crypto_box_PUBLICKEYBYTES) {
    throw std::runtime_error("Invalid sender public key size");
  }

  auto macKey = deriveMessageMacKey(senderPk, recipient.privateKey);
  auto macInput = buildContextMacInput(conversationId, senderId, recipientId, nonce, ciphertext);
  const int macResult =
      crypto_auth_verify(mac.data(), macInput.data(),
                         static_cast<unsigned long long>(macInput.size()), macKey.data());
  sodium_memzero(macKey.data(), macKey.size());
  if (macResult != 0) {
    throw std::runtime_error("Message context authentication failed");
  }

  std::vector<unsigned char> plaintext(ciphertext.size() - crypto_box_MACBYTES);
  if (crypto_box_open_easy(plaintext.data(), ciphertext.data(),
                           static_cast<unsigned long long>(ciphertext.size()), nonce.data(),
                           senderPk.data(), recipient.privateKey.data()) != 0) {
    throw std::runtime_error("Decryption failed");
  }

  return {reinterpret_cast<char*>(plaintext.data()), plaintext.size()};
}

std::filesystem::path LocalKeyStore::defaultKeyPath_(const std::string& username) {
  const char* home = std::getenv("HOME");
  if (home == nullptr || std::string(home).empty()) {
    throw std::runtime_error("HOME is not set; cannot choose local key file path");
  }

  return std::filesystem::path(home) / ".kingdom" / "keys" /
         (sanitizedUsername(username) + ".json");
}

}  // namespace kd
