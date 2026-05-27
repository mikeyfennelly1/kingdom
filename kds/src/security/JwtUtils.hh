#pragma once

#include <httplib.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sodium.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kd {

inline uint64_t epochSeconds() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
}

inline std::string base64UrlEncode(const unsigned char* data, size_t size) {
  std::string encoded(4 * ((size + 2) / 3), '\0');
  const int encodedSize = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), data,
                                          static_cast<int>(size));
  if (encodedSize < 0) {
    throw std::runtime_error("failed to base64url-encode JWT data");
  }
  encoded.resize(static_cast<size_t>(encodedSize));

  for (char& ch : encoded) {
    if (ch == '+') {
      ch = '-';
    } else if (ch == '/') {
      ch = '_';
    }
  }
  while (!encoded.empty() && encoded.back() == '=') {
    encoded.pop_back();
  }
  return encoded;
}

inline std::string base64UrlEncode(const std::string& data) {
  return base64UrlEncode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

inline std::optional<std::string> base64UrlDecode(const std::string& encoded) {
  std::string padded = encoded;
  for (char& ch : padded) {
    if (ch == '-') {
      ch = '+';
    } else if (ch == '_') {
      ch = '/';
    }
  }
  while (padded.size() % 4 != 0) {
    padded.push_back('=');
  }

  std::vector<unsigned char> decoded((padded.size() / 4) * 3 + 3);
  const int decodedSize =
      EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char*>(padded.data()),
                      static_cast<int>(padded.size()));
  if (decodedSize < 0) {
    return std::nullopt;
  }

  size_t padding = 0;
  if (!padded.empty() && padded[padded.size() - 1] == '=') {
    ++padding;
  }
  if (padded.size() > 1 && padded[padded.size() - 2] == '=') {
    ++padding;
  }
  decoded.resize(static_cast<size_t>(decodedSize) - padding);
  return std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
}

inline std::string signJwtInput(const std::string& signingInput, const std::string& secret) {
  unsigned char digest[EVP_MAX_MD_SIZE]{};
  unsigned int digestSize = 0;
  if (HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
           reinterpret_cast<const unsigned char*>(signingInput.data()), signingInput.size(), digest,
           &digestSize) == nullptr) {
    throw std::runtime_error("failed to sign JWT");
  }
  return base64UrlEncode(digest, digestSize);
}

inline std::optional<std::string> bearerToken(const httplib::Request& req) {
  const auto header = req.get_header_value("Authorization");
  constexpr std::string_view prefix = "Bearer ";
  if (header.size() <= prefix.size() || header.compare(0, prefix.size(), prefix) != 0) {
    return std::nullopt;
  }
  return header.substr(prefix.size());
}

inline std::optional<nlohmann::json> verifiedJwtPayload(const std::string& token,
                                                        const std::string& secret) {
  const auto firstDot = token.find('.');
  if (firstDot == std::string::npos) {
    return std::nullopt;
  }
  const auto secondDot = token.find('.', firstDot + 1);

  if (secondDot == std::string::npos || token.find('.', secondDot + 1) != std::string::npos) {
    return std::nullopt;
  }

  const auto signingInput = token.substr(0, secondDot);
  const auto signature = token.substr(secondDot + 1);
  const auto expectedSignature = signJwtInput(signingInput, secret);
  if (signature.size() != expectedSignature.size() ||
      CRYPTO_memcmp(signature.data(), expectedSignature.data(), signature.size()) != 0) {
    return std::nullopt;
  }

  const auto headerBody = base64UrlDecode(token.substr(0, firstDot));
  const auto payloadBody = base64UrlDecode(token.substr(firstDot + 1, secondDot - firstDot - 1));
  if (!headerBody.has_value() || !payloadBody.has_value()) {
    return std::nullopt;
  }

  auto header = nlohmann::json::parse(*headerBody, nullptr, false);
  auto payload = nlohmann::json::parse(*payloadBody, nullptr, false);
  if (header.is_discarded() || payload.is_discarded() || header.value("alg", "") != "HS256") {
    return std::nullopt;
  }
  if (!payload.contains("exp") || payload["exp"].get<uint64_t>() < epochSeconds()) {
    return std::nullopt;
  }
  return payload;
}

}  // namespace kd
