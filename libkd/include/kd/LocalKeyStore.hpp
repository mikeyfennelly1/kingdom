#pragma once

#include <filesystem>
#include <string>

namespace kd {

struct RegistrationKeyMaterial {
  std::string publicKey;
  std::string keyFilePath;
};

class LocalKeyStore {
 public:
  static RegistrationKeyMaterial createForSignup(const std::string& username,
                                                 const std::string& password);

 private:
  static std::filesystem::path defaultKeyPath_(const std::string& username);
};

}  // namespace kd
