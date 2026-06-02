#pragma once

#include <cstddef>

namespace kd {

namespace content_types {
inline constexpr const char* Json = "application/json";
}

namespace http_headers {
inline constexpr const char* Authorization = "Authorization";
inline constexpr const char* ContentType = "Content-Type";
inline constexpr const char* StrictTransportSecurity = "Strict-Transport-Security";
inline constexpr const char* XContentTypeOptions = "X-Content-Type-Options";
inline constexpr const char* XFrameOptions = "X-Frame-Options";

inline constexpr const char* HstsMaxAgeIncludeSubDomains = "max-age=31536000; includeSubDomains";
inline constexpr const char* NoSniff = "nosniff";
inline constexpr const char* Deny = "DENY";
}  // namespace http_headers

namespace jwt {
inline constexpr const char* AlgorithmHs256 = "HS256";
inline constexpr const char* BearerPrefix = "Bearer ";
inline constexpr const char* TypeJwt = "JWT";
}  // namespace jwt

namespace routes {
inline constexpr const char* Root = "/";
inline constexpr const char* Signup = "/signup";
inline constexpr const char* Login = "/login";
inline constexpr const char* Logout = "/logout";
inline constexpr const char* Health = "/health";
inline constexpr const char* Users = "/users";
inline constexpr const char* Api = "/api";
inline constexpr const char* SidecarRecord = "/record";
inline constexpr const char* UserPublicKey = R"(/users/(\d+)/public-key)";
inline constexpr const char* PublicUserKeyPathRegex = R"(^/users/[0-9]+/public-key$)";
}  // namespace routes

namespace security_predicates {
inline constexpr const char* ValidateSenderAuthenticity = "ValidateSenderAuthenticity";
inline constexpr const char* ValidateUntampered = "ValidateUntampered";
inline constexpr const char* ValidateAuthenticated = "ValidateAuthenticated";
}  // namespace security_predicates

namespace domain {
inline constexpr size_t kMinPasswordLen = 12;
inline constexpr size_t kMaxPasswordLen = 72;
inline constexpr size_t kMaxUsernameLen = 64;
inline constexpr size_t kMaxConversationNameLen = 128;
inline constexpr size_t kMaxPayloadLen = 65536;
inline constexpr size_t kMaxPublicKeyLen = 8192;
}  // namespace domain

namespace timeouts {
inline constexpr int kSidecarConnectionTimeoutSec = 30;
inline constexpr int kSidecarReadTimeoutSec = 60;
inline constexpr int kMessageReadTimeoutSec = 120;
inline constexpr int kRateLimitWindowSec = 60;
inline constexpr int kBlockchainResolverIntervalSec = 60;
}  // namespace timeouts

namespace blockchain {
inline constexpr const char* kPendingPrefix = "pending:";
inline constexpr size_t kPendingPrefixLen = 8;  // strlen("pending:")
}  // namespace blockchain

}  // namespace kd
