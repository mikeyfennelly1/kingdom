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
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace kd {
namespace {

constexpr unsigned long long kKdfOpsLimit = crypto_pwhash_OPSLIMIT_MODERATE;
constexpr size_t kKdfMemLimit = crypto_pwhash_MEMLIMIT_MODERATE;
constexpr const char* kKdfHkdfAlgorithm = "Argon2id+HKDF-SHA256";
constexpr const char* kKeyEncryptionInfo = "kd-key-encryption-v1";
constexpr const char* kBundleAlgorithm = "KD-X3DH-BUNDLE-V1";
constexpr const char* kPayloadAlgorithm = "KD-X3DH-XCHACHA20POLY1305-V1";
constexpr const char* kSignedPreKeySignatureInfo = "kd-x3dh-signed-prekey-signature-v1";
constexpr const char* kX3dhHkdfInfo = "kd-x3dh-message-key-v1";
constexpr const char* kX3dhHkdfSalt = "kd-x3dh-session-salt-v1";
constexpr size_t kOneTimePreKeyBatchSize = 20;

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
    const unsigned char* secret, size_t secretSize, std::string_view info, std::string_view salt) {
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

  char digestName[] = "SHA256";  // NOLINT(modernize-avoid-c-arrays)
  OSSL_PARAM params[] = {
      // NOLINT(modernize-avoid-c-arrays)
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digestName, 0),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<unsigned char*>(secret),
                                        secretSize),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(salt.data()),
                                        salt.size()),
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

std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> deriveKey(
    const std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>& sharedSecret,
    std::string_view info, std::string_view salt) {
  return deriveKey(sharedSecret.data(), sharedSecret.size(), info, salt);
}

void appendUint64Le(std::vector<unsigned char>& out, uint64_t value) {
  constexpr size_t kBitsPerByte = 8;
  constexpr uint64_t kByteMask = 0xff;
  for (size_t i = 0; i < sizeof(uint64_t); ++i) {
    out.push_back(static_cast<unsigned char>((value >> (i * kBitsPerByte)) & kByteMask));
  }
}

std::vector<unsigned char> signedPreKeySignatureInput(uint64_t signedPreKeyId,
                                                      const std::string& signedPreKeyPublicKey) {
  std::vector<unsigned char> input;
  const std::string_view info{kSignedPreKeySignatureInfo};
  input.insert(input.end(), info.begin(), info.end());
  appendUint64Le(input, signedPreKeyId);
  auto publicKey = base64Decode(signedPreKeyPublicKey, "signedPreKey.publicKey");
  input.insert(input.end(), publicKey.begin(), publicKey.end());
  return input;
}

std::string signSignedPreKey(uint64_t signedPreKeyId, const std::string& signedPreKeyPublicKey,
                             const std::array<unsigned char, kSigningSecretKeySize>& signingSk) {
  std::array<unsigned char, crypto_sign_BYTES> signature{};
  auto input = signedPreKeySignatureInput(signedPreKeyId, signedPreKeyPublicKey);
  crypto_sign_detached(signature.data(), nullptr, input.data(), input.size(), signingSk.data());
  return base64Encode(signature);
}

void verifySignedPreKeySignature(const nlohmann::json& bundle) {
  const auto signingKey =
      base64DecodeArray<crypto_sign_PUBLICKEYBYTES>(bundle.at("signingKey").get<std::string>(),
                                                    "signingKey");
  const auto& signedPreKey = bundle.at("signedPreKey");
  const auto signature =
      base64DecodeArray<crypto_sign_BYTES>(signedPreKey.at("signature").get<std::string>(),
                                           "signedPreKey.signature");
  auto input = signedPreKeySignatureInput(signedPreKey.at("id").get<uint64_t>(),
                                          signedPreKey.at("publicKey").get<std::string>());

  if (crypto_sign_verify_detached(signature.data(), input.data(), input.size(),
                                  signingKey.data()) != 0) {
    throw std::runtime_error("Signed prekey signature verification failed");
  }
}

nlohmann::json parseJson(const std::string& text, const std::string& label) {
  auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    throw std::runtime_error("Invalid JSON in " + label);
  }
  return parsed;
}

void validateBundle(const nlohmann::json& bundle) {
  if (!bundle.contains("version") || bundle.at("version").get<int>() != 1 ||
      !bundle.contains("algorithm") ||
      bundle.at("algorithm").get<std::string>() != kBundleAlgorithm ||
      !bundle.contains("identityKey") || !bundle.contains("signingKey") ||
      !bundle.contains("signedPreKey") || !bundle.contains("oneTimePreKeys")) {
    throw std::runtime_error("Unsupported or incomplete X3DH key bundle");
  }
  (void)base64DecodeArray<crypto_box_PUBLICKEYBYTES>(bundle.at("identityKey"), "identityKey");
  (void)base64DecodeArray<crypto_sign_PUBLICKEYBYTES>(bundle.at("signingKey"), "signingKey");
  (void)base64DecodeArray<crypto_box_PUBLICKEYBYTES>(bundle.at("signedPreKey").at("publicKey"),
                                                     "signedPreKey.publicKey");
  verifySignedPreKeySignature(bundle);
}

nlohmann::json publicBundle(const LocalIdentityKey& identity) {
  nlohmann::json oneTimePreKeys = nlohmann::json::array();
  for (const auto& preKey : identity.oneTimePreKeys) {
    oneTimePreKeys.push_back({{"id", preKey.id}, {"publicKey", preKey.publicKey}});
  }

  return {{"version", 1},
          {"algorithm", kBundleAlgorithm},
          {"identityKey", identity.publicKey},
          {"signingKey", identity.signingPublicKey},
          {"signedPreKey",
           {{"id", identity.signedPreKey.id},
            {"publicKey", identity.signedPreKey.publicKey},
            {"signature",
             signSignedPreKey(identity.signedPreKey.id, identity.signedPreKey.publicKey,
                              identity.signingSecretKey)}}},
          {"oneTimePreKeys", oneTimePreKeys}};
}

std::vector<unsigned char> dh(const std::array<unsigned char, kPrivateKeySize>& privateKey,
                              const std::string& publicKeyB64, const std::string& label) {
  const auto publicKey = base64DecodeArray<crypto_box_PUBLICKEYBYTES>(publicKeyB64, label);
  std::vector<unsigned char> output(crypto_scalarmult_BYTES);
  if (crypto_scalarmult(output.data(), privateKey.data(), publicKey.data()) != 0) {
    throw std::runtime_error("X25519 DH failed for " + label);
  }
  return output;
}

std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> deriveX3dhKey(
    const std::vector<std::vector<unsigned char>>& dhOutputs) {
  std::vector<unsigned char> ikm;
  ikm.reserve(dhOutputs.size() * crypto_scalarmult_BYTES);
  for (const auto& output : dhOutputs) {
    ikm.insert(ikm.end(), output.begin(), output.end());
  }
  auto key = deriveKey(ikm.data(), ikm.size(), kX3dhHkdfInfo, kX3dhHkdfSalt);
  sodium_memzero(ikm.data(), ikm.size());
  return key;
}

nlohmann::json associatedData(uint64_t conversationId, uint64_t senderId, uint64_t recipientId,
                              const std::string& senderIdentityPublicKey,
                              const std::string& senderEphemeralPublicKey,
                              const std::string& recipientIdentityPublicKey,
                              uint64_t signedPreKeyId,
                              const std::optional<uint64_t>& oneTimePreKeyId) {
  return {{"version", 1},
          {"algorithm", kPayloadAlgorithm},
          {"conversationId", conversationId},
          {"senderId", senderId},
          {"recipientId", recipientId},
          {"senderIdentityPublicKey", senderIdentityPublicKey},
          {"senderEphemeralPublicKey", senderEphemeralPublicKey},
          {"recipientIdentityPublicKey", recipientIdentityPublicKey},
          {"signedPreKeyId", signedPreKeyId},
          {"oneTimePreKeyId", oneTimePreKeyId.has_value() ? nlohmann::json(*oneTimePreKeyId)
                                                          : nlohmann::json(nullptr)}};
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

LocalPreKey makePreKey(uint64_t preKeyId) {
  LocalPreKey preKey;
  preKey.id = preKeyId;
  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> publicKey{};
  crypto_box_keypair(publicKey.data(), preKey.privateKey.data());
  preKey.publicKey = base64Encode(publicKey);
  return preKey;
}

nlohmann::json encryptedPrivateMaterial(const LocalIdentityKey& identity) {
  nlohmann::json oneTimePreKeys = nlohmann::json::array();
  for (const auto& preKey : identity.oneTimePreKeys) {
    oneTimePreKeys.push_back({{"id", preKey.id},
                              {"publicKey", preKey.publicKey},
                              {"privateKey", base64Encode(preKey.privateKey)}});
  }

  return {{"identityPrivateKey", base64Encode(identity.privateKey)},
          {"signingSecretKey", base64Encode(identity.signingSecretKey)},
          {"signedPreKey",
           {{"id", identity.signedPreKey.id},
            {"publicKey", identity.signedPreKey.publicKey},
            {"privateKey", base64Encode(identity.signedPreKey.privateKey)}}},
          {"oneTimePreKeys", oneTimePreKeys}};
}

void eraseOneTimePreKey(LocalIdentityKey& identity, uint64_t preKeyId) {
  auto iter = std::ranges::find_if(identity.oneTimePreKeys, [preKeyId](const LocalPreKey& key) {
    return key.id == preKeyId;
  });
  if (iter == identity.oneTimePreKeys.end()) {
    throw std::runtime_error("One-time prekey is missing or already used");
  }
  sodium_memzero(iter->privateKey.data(), iter->privateKey.size());
  identity.oneTimePreKeys.erase(iter);

  if (!identity.keyFilePath.empty()) {
    auto path = std::filesystem::path(identity.keyFilePath);
    auto file = readJsonFile(path);
    auto& used = file["usedOneTimePreKeyIds"];
    if (!used.is_array()) {
      used = nlohmann::json::array();
    }
    const bool alreadyRecorded = std::ranges::any_of(used, [preKeyId](const auto& usedId) {
      return usedId.template get<uint64_t>() == preKeyId;
    });
    if (!alreadyRecorded) {
      used.push_back(preKeyId);
      writeJsonFile(path, file);
    }
  }
}

}  // namespace

RegistrationKeyMaterial LocalKeyStore::createForSignup(const std::string& username,
                                                       const std::string& password) {
  ensureSodiumInitialized();

  auto keyPath = defaultKeyPath_(username);
  if (std::filesystem::exists(keyPath)) {
    throw std::runtime_error("Local key file already exists: " + keyPath.string());
  }

  LocalIdentityKey identity;
  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> identityPublic{};
  crypto_box_keypair(identityPublic.data(), identity.privateKey.data());
  identity.publicKey = base64Encode(identityPublic);

  std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> signingPublic{};
  crypto_sign_keypair(signingPublic.data(), identity.signingSecretKey.data());
  identity.signingPublicKey = base64Encode(signingPublic);

  identity.signedPreKey = makePreKey(1);
  for (size_t i = 0; i < kOneTimePreKeyBatchSize; ++i) {
    identity.oneTimePreKeys.push_back(makePreKey(i + 1));
  }

  std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
  randombytes_buf(salt.data(), salt.size());

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> argon2Secret{};
  if (crypto_pwhash(argon2Secret.data(), argon2Secret.size(), password.c_str(), password.size(),
                    salt.data(), kKdfOpsLimit, kKdfMemLimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive local key-encryption secret");
  }
  auto keyEncryptionKey = deriveKey(argon2Secret, kKeyEncryptionInfo, base64Encode(salt));
  sodium_memzero(argon2Secret.data(), argon2Secret.size());

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  const auto privateMaterial = encryptedPrivateMaterial(identity).dump();
  std::vector<unsigned char> ciphertext(privateMaterial.size() +
                                        crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertextSize = 0;
  const std::string& associatedDataText = username;
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      ciphertext.data(), &ciphertextSize,
      reinterpret_cast<const unsigned char*>(privateMaterial.data()), privateMaterial.size(),
      reinterpret_cast<const unsigned char*>(associatedDataText.data()), associatedDataText.size(),
      nullptr, nonce.data(), keyEncryptionKey.data());
  ciphertext.resize(ciphertextSize);
  sodium_memzero(keyEncryptionKey.data(), keyEncryptionKey.size());

  auto bundle = publicBundle(identity);
  nlohmann::json file = {{"version", 2},
                         {"username", username},
                         {"publicKeyBundle", bundle},
                         {"privateMaterial",
                          {{"algorithm", "XChaCha20-Poly1305"},
                           {"ciphertext", base64Encode(ciphertext.data(), ciphertext.size())},
                           {"nonce", base64Encode(nonce)}}},
                         {"kdf",
                          {{"algorithm", kKdfHkdfAlgorithm},
                           {"hkdf", {{"digest", "SHA-256"}, {"info", kKeyEncryptionInfo}}},
                           {"salt", base64Encode(salt)},
                           {"opsLimit", kKdfOpsLimit},
                           {"memLimit", kKdfMemLimit}}},
                         {"usedOneTimePreKeyIds", nlohmann::json::array()}};

  writeJsonFile(keyPath, file);
  return RegistrationKeyMaterial{.publicKeyBundle = bundle.dump(), .keyFilePath = keyPath.string()};
}

LocalIdentityKey LocalKeyStore::loadForLogin(const std::string& username,
                                             const std::string& password) {
  ensureSodiumInitialized();

  auto keyPath = defaultKeyPath_(username);
  auto file = readJsonFile(keyPath);
  if (!file.contains("version") || file.at("version").get<int>() != 2 ||
      !file.contains("username") || file.at("username").get<std::string>() != username ||
      !file.contains("publicKeyBundle") || !file.contains("privateMaterial") ||
      !file.contains("kdf")) {
    throw std::runtime_error("Unsupported local key file format: " + keyPath.string());
  }

  auto bundle = file.at("publicKeyBundle");
  validateBundle(bundle);

  const auto& privateJson = file.at("privateMaterial");
  const auto& kdfJson = file.at("kdf");
  if (privateJson.at("algorithm").get<std::string>() != "XChaCha20-Poly1305" ||
      kdfJson.at("algorithm").get<std::string>() != kKdfHkdfAlgorithm ||
      kdfJson.at("hkdf").at("digest").get<std::string>() != "SHA-256" ||
      kdfJson.at("hkdf").at("info").get<std::string>() != kKeyEncryptionInfo) {
    throw std::runtime_error("Unsupported local key file crypto parameters: " + keyPath.string());
  }

  auto salt = base64DecodeArray<crypto_pwhash_SALTBYTES>(kdfJson.at("salt"), "kdf.salt");
  auto nonce =
      base64DecodeArray<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(privateJson.at("nonce"),
                                                                      "privateMaterial.nonce");
  auto ciphertext = base64Decode(privateJson.at("ciphertext"), "privateMaterial.ciphertext");

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> argon2Secret{};
  if (crypto_pwhash(argon2Secret.data(), argon2Secret.size(), password.c_str(), password.size(),
                    salt.data(), kdfJson.at("opsLimit").get<unsigned long long>(),
                    kdfJson.at("memLimit").get<size_t>(), crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive local key-encryption secret");
  }
  auto keyEncryptionKey = deriveKey(argon2Secret, kKeyEncryptionInfo, base64Encode(salt));
  sodium_memzero(argon2Secret.data(), argon2Secret.size());

  std::vector<unsigned char> plaintext(ciphertext.size());
  unsigned long long plaintextSize = 0;
  const std::string& associatedDataText = username;
  const auto decryptResult = crypto_aead_xchacha20poly1305_ietf_decrypt(
      plaintext.data(), &plaintextSize, nullptr, ciphertext.data(), ciphertext.size(),
      reinterpret_cast<const unsigned char*>(associatedDataText.data()), associatedDataText.size(),
      nonce.data(), keyEncryptionKey.data());
  sodium_memzero(keyEncryptionKey.data(), keyEncryptionKey.size());
  if (decryptResult != 0) {
    throw std::runtime_error("Failed to decrypt local private key for user: " + username);
  }
  plaintext.resize(plaintextSize);

  auto privateMaterial =
      parseJson(std::string(reinterpret_cast<const char*>(plaintext.data()), plaintext.size()),
                "privateMaterial");
  sodium_memzero(plaintext.data(), plaintext.size());

  LocalIdentityKey identity;
  identity.publicKey = bundle.at("identityKey").get<std::string>();
  identity.privateKey = base64DecodeArray<kPrivateKeySize>(privateMaterial.at("identityPrivateKey"),
                                                           "identityPrivateKey");
  identity.signingPublicKey = bundle.at("signingKey").get<std::string>();
  identity.signingSecretKey =
      base64DecodeArray<kSigningSecretKeySize>(privateMaterial.at("signingSecretKey"),
                                               "signingSecretKey");
  identity.signedPreKey.id = privateMaterial.at("signedPreKey").at("id").get<uint64_t>();
  identity.signedPreKey.publicKey =
      privateMaterial.at("signedPreKey").at("publicKey").get<std::string>();
  identity.signedPreKey.privateKey =
      base64DecodeArray<kPrivateKeySize>(privateMaterial.at("signedPreKey").at("privateKey"),
                                         "signedPreKey.privateKey");
  identity.keyFilePath = keyPath.string();

  std::vector<uint64_t> usedIds;
  if (file.contains("usedOneTimePreKeyIds") && file.at("usedOneTimePreKeyIds").is_array()) {
    for (const auto& usedEntry : file.at("usedOneTimePreKeyIds")) {
      usedIds.push_back(usedEntry.get<uint64_t>());
    }
  }

  for (const auto& preKeyJson : privateMaterial.at("oneTimePreKeys")) {
    const auto preKeyId = preKeyJson.at("id").get<uint64_t>();
    if (std::ranges::find(usedIds, preKeyId) != usedIds.end()) {
      continue;
    }
    LocalPreKey preKey;
    preKey.id = preKeyId;
    preKey.publicKey = preKeyJson.at("publicKey").get<std::string>();
    preKey.privateKey =
        base64DecodeArray<kPrivateKeySize>(preKeyJson.at("privateKey"), "oneTimePreKey.privateKey");
    identity.oneTimePreKeys.push_back(preKey);
  }

  return identity;
}

std::string LocalKeyStore::encryptMessage(const std::string& plaintext,
                                          const LocalIdentityKey& sender,
                                          const std::string& recipientBundleJson,
                                          uint64_t conversationId, uint64_t senderId,
                                          uint64_t recipientId) {
  ensureSodiumInitialized();

  auto recipientBundle = parseJson(recipientBundleJson, "recipientBundle");
  validateBundle(recipientBundle);

  const auto recipientIdentityPublicKey = recipientBundle.at("identityKey").get<std::string>();
  const auto signedPreKeyId = recipientBundle.at("signedPreKey").at("id").get<uint64_t>();
  const auto signedPreKeyPublicKey =
      recipientBundle.at("signedPreKey").at("publicKey").get<std::string>();

  std::optional<uint64_t> oneTimePreKeyId;
  std::optional<std::string> oneTimePreKeyPublicKey;
  const auto& oneTimePreKeys = recipientBundle.at("oneTimePreKeys");
  if (oneTimePreKeys.is_array() && !oneTimePreKeys.empty()) {
    oneTimePreKeyId = oneTimePreKeys.front().at("id").get<uint64_t>();
    oneTimePreKeyPublicKey = oneTimePreKeys.front().at("publicKey").get<std::string>();
  }

  std::array<unsigned char, crypto_box_PUBLICKEYBYTES> ephemeralPublic{};
  std::array<unsigned char, crypto_box_SECRETKEYBYTES> ephemeralPrivate{};
  crypto_box_keypair(ephemeralPublic.data(), ephemeralPrivate.data());
  const auto ephemeralPublicKey = base64Encode(ephemeralPublic);

  std::vector<std::vector<unsigned char>> dhOutputs;
  dhOutputs.push_back(dh(sender.privateKey, signedPreKeyPublicKey, "recipient.signedPreKey"));
  dhOutputs.push_back(dh(ephemeralPrivate, recipientIdentityPublicKey, "recipient.identityKey"));
  dhOutputs.push_back(dh(ephemeralPrivate, signedPreKeyPublicKey, "recipient.signedPreKey"));
  if (oneTimePreKeyPublicKey.has_value()) {
    dhOutputs.push_back(dh(ephemeralPrivate, *oneTimePreKeyPublicKey, "recipient.oneTimePreKey"));
  }

  auto messageKey = deriveX3dhKey(dhOutputs);
  sodium_memzero(ephemeralPrivate.data(), ephemeralPrivate.size());
  for (auto& output : dhOutputs) {
    sodium_memzero(output.data(), output.size());
  }

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  const auto aad =
      associatedData(conversationId, senderId, recipientId, sender.publicKey, ephemeralPublicKey,
                     recipientIdentityPublicKey, signedPreKeyId, oneTimePreKeyId)
          .dump();
  std::vector<unsigned char> ciphertext(plaintext.size() +
                                        crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertextSize = 0;
  if (crypto_aead_xchacha20poly1305_ietf_encrypt(
          ciphertext.data(), &ciphertextSize,
          reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
          reinterpret_cast<const unsigned char*>(aad.data()), aad.size(), nullptr, nonce.data(),
          messageKey.data()) != 0) {
    sodium_memzero(messageKey.data(), messageKey.size());
    throw std::runtime_error("Encryption failed");
  }
  sodium_memzero(messageKey.data(), messageKey.size());
  ciphertext.resize(ciphertextSize);

  return nlohmann::json{{"version", 1},
                        {"algorithm", kPayloadAlgorithm},
                        {"senderIdentityPublicKey", sender.publicKey},
                        {"senderEphemeralPublicKey", ephemeralPublicKey},
                        {"signedPreKeyId", signedPreKeyId},
                        {"oneTimePreKeyId", oneTimePreKeyId.has_value()
                                                ? nlohmann::json(*oneTimePreKeyId)
                                                : nlohmann::json(nullptr)},
                        {"nonce", base64Encode(nonce)},
                        {"ciphertext", base64Encode(ciphertext.data(), ciphertext.size())}}
      .dump();
}

std::string LocalKeyStore::decryptMessage(const std::string& payload, LocalIdentityKey& recipient,
                                          const std::string& senderBundleJson,
                                          uint64_t conversationId, uint64_t senderId,
                                          uint64_t recipientId) {
  ensureSodiumInitialized();

  auto payloadJson = parseJson(payload, "message payload");
  if (payloadJson.at("version").get<int>() != 1 ||
      payloadJson.at("algorithm").get<std::string>() != kPayloadAlgorithm) {
    throw std::runtime_error("Unsupported message payload version");
  }

  auto senderBundle = parseJson(senderBundleJson, "senderBundle");
  validateBundle(senderBundle);
  const auto senderIdentityPublicKey = payloadJson.at("senderIdentityPublicKey").get<std::string>();
  if (senderBundle.at("identityKey").get<std::string>() != senderIdentityPublicKey) {
    throw std::runtime_error("Sender identity key does not match pinned bundle");
  }

  const auto senderEphemeralPublicKey =
      payloadJson.at("senderEphemeralPublicKey").get<std::string>();
  const auto signedPreKeyId = payloadJson.at("signedPreKeyId").get<uint64_t>();
  if (recipient.signedPreKey.id != signedPreKeyId) {
    throw std::runtime_error("Recipient signed prekey is missing");
  }

  std::optional<uint64_t> oneTimePreKeyId;
  std::optional<LocalPreKey> oneTimePreKey;
  if (!payloadJson.at("oneTimePreKeyId").is_null()) {
    oneTimePreKeyId = payloadJson.at("oneTimePreKeyId").get<uint64_t>();
    auto iter =
        std::ranges::find_if(recipient.oneTimePreKeys, [oneTimePreKeyId](const LocalPreKey& key) {
          return key.id == *oneTimePreKeyId;
        });
    if (iter == recipient.oneTimePreKeys.end()) {
      throw std::runtime_error("One-time prekey is missing or already used");
    }
    oneTimePreKey = *iter;
  }

  std::vector<std::vector<unsigned char>> dhOutputs;
  dhOutputs.push_back(
      dh(recipient.signedPreKey.privateKey, senderIdentityPublicKey, "sender.identityKey"));
  dhOutputs.push_back(dh(recipient.privateKey, senderEphemeralPublicKey, "sender.ephemeralKey"));
  dhOutputs.push_back(
      dh(recipient.signedPreKey.privateKey, senderEphemeralPublicKey, "sender.ephemeralKey"));
  if (oneTimePreKey.has_value()) {
    dhOutputs.push_back(
        dh(oneTimePreKey->privateKey, senderEphemeralPublicKey, "sender.ephemeralKey"));
  }

  auto messageKey = deriveX3dhKey(dhOutputs);
  for (auto& output : dhOutputs) {
    sodium_memzero(output.data(), output.size());
  }

  const auto nonce =
      base64DecodeArray<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(payloadJson.at("nonce"),
                                                                      "payload.nonce");
  const auto ciphertext = base64Decode(payloadJson.at("ciphertext"), "payload.ciphertext");
  if (ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
    sodium_memzero(messageKey.data(), messageKey.size());
    throw std::runtime_error("Ciphertext too short");
  }

  const auto aad =
      associatedData(conversationId, senderId, recipientId, senderIdentityPublicKey,
                     senderEphemeralPublicKey, recipient.publicKey, signedPreKeyId, oneTimePreKeyId)
          .dump();
  std::vector<unsigned char> plaintext(ciphertext.size() -
                                       crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long plaintextSize = 0;
  const auto decryptResult =
      crypto_aead_xchacha20poly1305_ietf_decrypt(plaintext.data(), &plaintextSize, nullptr,
                                                 ciphertext.data(), ciphertext.size(),
                                                 reinterpret_cast<const unsigned char*>(aad.data()),
                                                 aad.size(), nonce.data(), messageKey.data());
  sodium_memzero(messageKey.data(), messageKey.size());
  if (decryptResult != 0) {
    throw std::runtime_error("Decryption failed");
  }
  plaintext.resize(plaintextSize);

  if (oneTimePreKeyId.has_value()) {
    eraseOneTimePreKey(recipient, *oneTimePreKeyId);
  }

  return {reinterpret_cast<char*>(plaintext.data()), plaintext.size()};
}

std::optional<uint64_t> LocalKeyStore::oneTimePreKeyIdFromPayload(const std::string& payload) {
  auto payloadJson = parseJson(payload, "message payload");
  if (!payloadJson.contains("oneTimePreKeyId") || payloadJson.at("oneTimePreKeyId").is_null()) {
    return std::nullopt;
  }
  return payloadJson.at("oneTimePreKeyId").get<uint64_t>();
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
