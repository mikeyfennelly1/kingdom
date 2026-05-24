#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

namespace kd {

constexpr size_t kPrivateKeySize = 32;

struct RegistrationKeyMaterial {
  std::string publicKey;
  std::string keyFilePath;
};

struct LocalIdentityKey {
  std::string publicKey;
  std::array<unsigned char, kPrivateKeySize> privateKey;
};

class LocalKeyStore {
 public:
  static RegistrationKeyMaterial createForSignup(const std::string& username,
                                                 const std::string& password);
  static LocalIdentityKey loadForLogin(const std::string& username, const std::string& password);

  // Encrypt plaintext for a recipient. Returns base64(nonce || ciphertext).
  static std::string encryptMessage(const std::string& plaintext, const LocalIdentityKey& sender,
                                    const std::string& recipientPkB64);

  // Decrypt a payload produced by encryptMessage. Returns plaintext.
  // Throws on decryption failure (wrong key, tampered ciphertext).
  static std::string decryptMessage(const std::string& payloadB64,
                                    const LocalIdentityKey& recipient,
                                    const std::string& senderPkB64);

 private:
  static std::filesystem::path defaultKeyPath_(const std::string& username);
};

}  // namespace kd
