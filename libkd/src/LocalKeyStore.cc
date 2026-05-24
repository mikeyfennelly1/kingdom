#include "kd/LocalKeyStore.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace kd {
namespace {

constexpr unsigned long long kKdfOpsLimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr size_t kKdfMemLimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;

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

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> keyEncryptionKey{};
  if (crypto_pwhash(keyEncryptionKey.data(), keyEncryptionKey.size(), password.c_str(),
                    password.size(), salt.data(), kKdfOpsLimit, kKdfMemLimit,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive local key-encryption key");
  }

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
                          {{"algorithm", "Argon2id"},
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
  if (privateKeyJson["algorithm"].get<std::string>() != "XChaCha20-Poly1305" ||
      kdfJson["algorithm"].get<std::string>() != "Argon2id") {
    throw std::runtime_error("Unsupported local key file crypto parameters: " + keyPath.string());
  }

  auto salt =
      base64DecodeArray<crypto_pwhash_SALTBYTES>(kdfJson["salt"].get<std::string>(), "kdf.salt");
  auto nonce = base64DecodeArray<crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>(
      privateKeyJson["nonce"].get<std::string>(), "privateKey.nonce");
  auto ciphertext =
      base64Decode(privateKeyJson["ciphertext"].get<std::string>(), "privateKey.ciphertext");
  auto opsLimit = kdfJson["opsLimit"].get<unsigned long long>();
  auto memLimit = kdfJson["memLimit"].get<size_t>();

  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> keyEncryptionKey{};
  if (crypto_pwhash(keyEncryptionKey.data(), keyEncryptionKey.size(), password.c_str(),
                    password.size(), salt.data(), opsLimit, memLimit,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to derive local key-encryption key");
  }

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
                                           const std::string& recipientPkB64) {
  ensureSodiumInitialized();

  auto recipientPk = base64Decode(recipientPkB64, "recipientPublicKey");
  if (recipientPk.size() != crypto_box_PUBLICKEYBYTES) {
    throw std::runtime_error("Invalid recipient public key size");
  }

  std::array<unsigned char, crypto_box_NONCEBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  std::vector<unsigned char> ciphertext(plaintext.size() + crypto_box_MACBYTES);
  if (crypto_box_easy(ciphertext.data(),
                      reinterpret_cast<const unsigned char*>(plaintext.data()),
                      static_cast<unsigned long long>(plaintext.size()), nonce.data(),
                      recipientPk.data(), sender.privateKey.data()) != 0) {
    throw std::runtime_error("Encryption failed");
  }

  std::vector<unsigned char> combined;
  combined.insert(combined.end(), nonce.begin(), nonce.end());
  combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
  return base64Encode(combined.data(), combined.size());
}

std::string LocalKeyStore::decryptMessage(const std::string& payloadB64,
                                           const LocalIdentityKey& recipient,
                                           const std::string& senderPkB64) {
  ensureSodiumInitialized();

  auto combined = base64Decode(payloadB64, "payload");
  if (combined.size() < crypto_box_NONCEBYTES + crypto_box_MACBYTES) {
    throw std::runtime_error("Payload too short to be valid ciphertext");
  }

  std::array<unsigned char, crypto_box_NONCEBYTES> nonce{};
  std::copy(combined.begin(), combined.begin() + crypto_box_NONCEBYTES, nonce.begin());

  auto senderPk = base64Decode(senderPkB64, "senderPublicKey");
  if (senderPk.size() != crypto_box_PUBLICKEYBYTES) {
    throw std::runtime_error("Invalid sender public key size");
  }

  const size_t ciphertextLen = combined.size() - crypto_box_NONCEBYTES;
  std::vector<unsigned char> plaintext(ciphertextLen - crypto_box_MACBYTES);
  if (crypto_box_open_easy(plaintext.data(), combined.data() + crypto_box_NONCEBYTES,
                           static_cast<unsigned long long>(ciphertextLen), nonce.data(),
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
