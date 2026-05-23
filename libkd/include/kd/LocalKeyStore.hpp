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

 private:
  static std::filesystem::path defaultKeyPath_(const std::string& username);
};

}  // namespace kd
