#include "kd/LocalKeyStore.hpp"

#include <sodium.h>

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

std::filesystem::path LocalKeyStore::defaultKeyPath_(const std::string& username) {
  const char* home = std::getenv("HOME");
  if (home == nullptr || std::string(home).empty()) {
    throw std::runtime_error("HOME is not set; cannot choose local key file path");
  }

  return std::filesystem::path(home) / ".kingdom" / "keys" /
         (sanitizedUsername(username) + ".json");
}

}  // namespace kd
