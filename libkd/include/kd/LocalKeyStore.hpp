#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kd {

constexpr size_t kPrivateKeySize = 32;
constexpr size_t kSigningPublicKeySize = 32;
constexpr size_t kSigningSecretKeySize = 64;

struct LocalPreKey {
  uint64_t id = 0;
  std::string publicKey;
  std::array<unsigned char, kPrivateKeySize> privateKey{};
};

struct RegistrationKeyMaterial {
  std::string publicKeyBundle;
  std::string keyFilePath;
};

struct LocalIdentityKey {
  std::string publicKey;
  std::array<unsigned char, kPrivateKeySize> privateKey{};
  std::string signingPublicKey;
  std::array<unsigned char, kSigningSecretKeySize> signingSecretKey{};
  LocalPreKey signedPreKey;
  std::vector<LocalPreKey> oneTimePreKeys;
  std::string keyFilePath;
};

class LocalKeyStore {
 public:
  static RegistrationKeyMaterial createForSignup(const std::string& username,
                                                 const std::string& password);
  static LocalIdentityKey loadForLogin(const std::string& username, const std::string& password);

  // Encrypt plaintext for a recipient key bundle. Returns a versioned JSON X3DH payload.
  static std::string encryptMessage(const std::string& plaintext, const LocalIdentityKey& sender,
                                    const std::string& recipientBundleJson, uint64_t conversationId,
                                    uint64_t senderId, uint64_t recipientId);

  // Decrypt a payload produced by encryptMessage. Returns plaintext.
  // Throws on decryption failure (wrong key, tampered ciphertext, wrong context).
  static std::string decryptMessage(const std::string& payload, LocalIdentityKey& recipient,
                                    const std::string& senderBundleJson, uint64_t conversationId,
                                    uint64_t senderId, uint64_t recipientId);

  static std::optional<uint64_t> oneTimePreKeyIdFromPayload(const std::string& payload);

 private:
  static std::filesystem::path defaultKeyPath_(const std::string& username);
};

}  // namespace kd
