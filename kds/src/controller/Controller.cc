#include "Controller.hh"

#include <sodium.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <kd/User.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "../common/Constants.hh"
#include "../security/JwtUtils.hh"
#include "../security/SecurityPredicates.hh"

namespace kd {

namespace {

namespace json_fields {
constexpr const char* Alg = "alg";
constexpr const char* Ciphertext = "ciphertext";
constexpr const char* ConversationId = "conversationId";
constexpr const char* Error = "error";
constexpr const char* Exp = "exp";
constexpr const char* Iat = "iat";
constexpr const char* Id = "id";
constexpr const char* Message = "message";
constexpr const char* MessageId = "messageId";
constexpr const char* MsgId = "msgId";
constexpr const char* Name = "name";
constexpr const char* OneTimePreKeyId = "oneTimePreKeyId";
constexpr const char* ParticipantIds = "participantIds";
constexpr const char* Password = "password";
constexpr const char* Payload = "payload";
constexpr const char* PublicKey = "publicKey";
constexpr const char* RecipientId = "recipientId";
constexpr const char* SenderId = "senderId";
constexpr const char* SessionToken = "sessionToken";
constexpr const char* Status = "status";
constexpr const char* Sub = "sub";
constexpr const char* Token = "token";
constexpr const char* TxHash = "txHash";
constexpr const char* Typ = "typ";
constexpr const char* UserId = "userId";
constexpr const char* Username = "username";
constexpr const char* Version = "version";
}  // namespace json_fields

namespace api_info {
constexpr const char* Name = "Kingdom Server";
constexpr const char* Version = "1.0";
}  // namespace api_info

std::optional<uint64_t> oneTimePreKeyIdFromPayload(const std::string& payload) {
  auto payloadJson = nlohmann::json::parse(payload, nullptr, false);
  if (payloadJson.is_discarded() || !payloadJson.is_object() ||
      !payloadJson.contains(json_fields::OneTimePreKeyId) ||
      payloadJson[json_fields::OneTimePreKeyId].is_null()) {
    return std::nullopt;
  }
  if (!payloadJson[json_fields::OneTimePreKeyId].is_number_unsigned()) {
    return std::nullopt;
  }
  return payloadJson[json_fields::OneTimePreKeyId].get<uint64_t>();
}

bool isValidSignupPassword(const std::string& password) {
  if (password.size() < 12 || password.size() > 72) {
    return false;
  }

  bool hasUppercase = false;
  bool hasNumber = false;
  for (unsigned char ch : password) {
    hasUppercase = hasUppercase || std::isupper(ch) != 0;
    hasNumber = hasNumber || std::isdigit(ch) != 0;
  }

  return hasUppercase && hasNumber;
}

}  // namespace

Controller::Controller(std::string host, int port, std::string dbConnectionString,
                       std::string sidecarUrl, std::string certPath, std::string keyPath,
                       std::string jwtSecret, uint64_t jwtTtlSeconds)
    : host_(std::move(host))
    , port_(port)
    , sidecarUrl_(std::move(sidecarUrl))
    , svr_(certPath.c_str(), keyPath.c_str())
    , db_(dbConnectionString)
    , jwtSecret_(std::move(jwtSecret))
    , jwtTtlSeconds_(jwtTtlSeconds) {
  if (sodium_init() < 0) {
    throw std::runtime_error("Failed to initialize libsodium");
  }
  std::vector<std::string> securityPredicates = {security_predicates::ValidateSenderAuthenticity,
                                                 security_predicates::ValidateUntampered,
                                                 security_predicates::ValidateAuthenticated};
  securityFilterChain_ = std::make_unique<SecurityFilterChain>(securityPredicates);

  setupRoutes();
}

void Controller::setupRoutes() {
  svr_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    auto error = securityFilterChain_->Execute(req);
    if (error.has_value()) {
      nlohmann::json errorJson = {{json_fields::Error, error->message}};
      res.status = error->httpStatusCode;
      res.set_content(errorJson.dump(), content_types::Json);
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  svr_.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
    res.set_header(http_headers::StrictTransportSecurity,
                   http_headers::HstsMaxAgeIncludeSubDomains);
    res.set_header(http_headers::XContentTypeOptions, http_headers::NoSniff);
    res.set_header(http_headers::XFrameOptions, http_headers::Deny);
  });

  notFoundHandler_();
  healthController_();
  authController_();
  userController_();
  publicKeyController_();
  basicApiInfo_();

  // conversations
  conversationController_();
  messageController_();
}

std::string Controller::createSession_(uint64_t userId, const std::string& username) const {
  const auto issuedAt = epochSeconds();
  nlohmann::json header = {{json_fields::Alg, jwt::AlgorithmHs256},
                           {json_fields::Typ, jwt::TypeJwt}};
  nlohmann::json payload = {{json_fields::Sub, std::to_string(userId)},
                            {json_fields::Username, username},
                            {json_fields::Iat, issuedAt},
                            {json_fields::Exp, issuedAt + jwtTtlSeconds_}};

  auto signingInput = base64UrlEncode(header.dump()) + "." + base64UrlEncode(payload.dump());
  return signingInput + "." + signJwtInput(signingInput, jwtSecret_);
}

std::optional<uint64_t> Controller::authenticatedUserId_(const httplib::Request& req) {
  auto token = bearerToken(req);
  if (!token.has_value()) {
    return std::nullopt;
  }

  auto payload = verifiedJwtPayload(*token, jwtSecret_);
  if (!payload.has_value()) {
    return std::nullopt;
  }
  try {
    return std::stoull((*payload)[json_fields::Sub].get<std::string>());
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool Controller::isRateLimited_(const std::string& ip) {
  constexpr int kMaxAttempts = 10;
  constexpr auto kWindow = std::chrono::seconds(60);

  std::lock_guard<std::mutex> lock(rateLimitMutex_);
  auto now = std::chrono::steady_clock::now();
  auto& entry = rateLimitMap_[ip];

  if (now - entry.windowStart >= kWindow) {
    entry.count = 0;
    entry.windowStart = now;
  }

  ++entry.count;
  return entry.count > kMaxAttempts;
}

void Controller::authController_() {
  svr_.Post(routes::Signup, [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Sign-up request received");

    if (isRateLimited_(req.remote_addr)) {
      res.status = httplib::TooManyRequests_429;
      res.set_content(nlohmann::json{{json_fields::Error, "too many requests"}}.dump(),
                      content_types::Json);
      return;
    }

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains(json_fields::Username) ||
        !body.contains(json_fields::Password) || !body.contains(json_fields::PublicKey)) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "username, password, and publicKey required"}}.dump(),
          content_types::Json);
      return;
    }

    std::string username = body[json_fields::Username];
    std::string password = body[json_fields::Password];
    std::string publicKey = body[json_fields::PublicKey];

    if (username.empty() || username.size() > 64) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "username must be 1–64 characters"}}.dump(),
          content_types::Json);
      return;
    }
    if (!isValidSignupPassword(password)) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{
              {json_fields::Error,
               "password must be 12–72 characters and include at least one uppercase letter and "
               "one number"}}
              .dump(),
          content_types::Json);
      return;
    }
    if (publicKey.empty()) {
      res.status = httplib::BadRequest_400;
      res.set_content(nlohmann::json{{json_fields::Error, "publicKey must not be empty"}}.dump(),
                      content_types::Json);
      return;
    }

    try {
      auto passwordHash = User::hashPassword(password);
      uint64_t id = db_.createUser(username, passwordHash, publicKey);
      auto sessionToken = createSession_(id, username);
      spdlog::info("Created user '{}' with id {}", username, id);
      res.status = httplib::Created_201;
      res.set_content(nlohmann::json{{json_fields::Id, id},
                                     {json_fields::Username, username},
                                     {json_fields::PublicKey, publicKey},
                                     {json_fields::Token, sessionToken},
                                     {json_fields::SessionToken, sessionToken}}
                          .dump(),
                      content_types::Json);
    } catch (const std::runtime_error& e) {
      res.status = httplib::Conflict_409;
      res.set_content(nlohmann::json{{json_fields::Error, e.what()}}.dump(), content_types::Json);
    }
  });

  // Login endpoint
  svr_.Post(routes::Login, [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Login request received from {}", req.remote_addr);

    if (isRateLimited_(req.remote_addr)) {
      res.status = httplib::TooManyRequests_429;
      res.set_content(nlohmann::json{{json_fields::Error, "too many requests"}}.dump(),
                      content_types::Json);
      return;
    }

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains(json_fields::Username) ||
        !body.contains(json_fields::Password)) {
      res.status = httplib::BadRequest_400;
      res.set_content(nlohmann::json{{json_fields::Error, "username and password required"}}.dump(),
                      content_types::Json);
      return;
    }

    std::string username = body[json_fields::Username];
    std::string password = body[json_fields::Password];

    if (username.empty() || username.size() > 64) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "username must be 1–64 characters"}}.dump(),
          content_types::Json);
      return;
    }
    if (password.empty() || password.size() > 72) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "password must be 1–72 characters"}}.dump(),
          content_types::Json);
      return;
    }

    auto user = db_.getUserByUsername(username);
    if (!user.has_value() || !user->verifyPassword(password)) {
      res.status = httplib::Unauthorized_401;
      res.set_content(nlohmann::json{{json_fields::Error, "invalid username or password"}}.dump(),
                      content_types::Json);
      return;
    }

    auto sessionToken = createSession_(user->id(), user->username());
    res.set_content(nlohmann::json{{json_fields::Id, user->id()},
                                   {json_fields::Username, user->username()},
                                   {json_fields::Token, sessionToken},
                                   {json_fields::SessionToken, sessionToken}}
                        .dump(),
                    content_types::Json);
  });

  // Logout endpoint — revokes the token server-side so it cannot be reused
  svr_.Post(routes::Logout, [](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Logout request received");
    auto token = bearerToken(req);
    if (token.has_value()) {
      TokenBlacklist::revoke(*token);
      spdlog::info("Token revoked");
    }
    nlohmann::json response = {{json_fields::Status, "success"},
                               {json_fields::Message, "Logged out"}};
    res.set_content(response.dump(), content_types::Json);
  });
}

void Controller::start() {
  spdlog::info("Starting Kingdom Server on {}:{}", host_, port_);

  if (!svr_.listen(host_, port_)) {
    spdlog::error("Failed to start server on {}:{}", host_, port_);
    return;
  }
}

void Controller::healthController_() {
  svr_.Get(routes::Health, [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json status = {{json_fields::Status, "ok"}};
    res.set_content(status.dump(), content_types::Json);
  });
}

void Controller::userController_() {
  svr_.Get(routes::Users, [this](const httplib::Request&, httplib::Response& res) -> void {
    auto users = db_.getAllUsers();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& u : users) {
      arr.push_back({{json_fields::Id, u.id()}, {json_fields::Username, u.username()}});
    }
    res.set_content(arr.dump(), content_types::Json);
  });
}

void Controller::publicKeyController_() {
  svr_.Get(routes::UserPublicKey,
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             uint64_t userId = std::stoull(std::string(req.matches[1]));
             auto publicKey = db_.getUserPublicKey(userId);
             if (!publicKey.has_value()) {
               res.status = httplib::NotFound_404;
               res.set_content(nlohmann::json{{json_fields::Error, "user not found"}}.dump(),
                               content_types::Json);
               return;
             }
             res.set_content(nlohmann::json{{json_fields::UserId, userId},
                                            {json_fields::PublicKey, *publicKey}}
                                 .dump(),
                             content_types::Json);
           });
}

void Controller::conversationController_() {
  svr_.Post("/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    auto authenticatedUserId = authenticatedUserId_(req);
    if (!authenticatedUserId.has_value()) {
      res.status = httplib::Unauthorized_401;
      res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                      content_types::Json);
      return;
    }

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains(json_fields::Name) ||
        !body.contains(json_fields::ParticipantIds)) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "name and participantIds required"}}.dump(),
          content_types::Json);
      return;
    }

    std::string name = body[json_fields::Name];

    if (name.empty() || name.size() > 128) {
      res.status = httplib::BadRequest_400;
      res.set_content(nlohmann::json{{json_fields::Error, "name must be 1–128 characters"}}.dump(),
                      content_types::Json);
      return;
    }

    auto participantIds = body[json_fields::ParticipantIds].get<std::vector<uint64_t>>();

    std::set<uint64_t> seen(participantIds.begin(), participantIds.end());
    if (seen.size() != participantIds.size()) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "participantIds must not contain duplicates"}}.dump(),
          content_types::Json);
      return;
    }

    uint64_t id = db_.createConversation(name, participantIds);
    spdlog::info("Created conversation '{}' with id {}", name, id);
    res.status = httplib::Created_201;
    res.set_content(nlohmann::json{{json_fields::Id, id}, {json_fields::Name, name}}.dump(),
                    content_types::Json);
  });

  svr_.Get(R"(/users/(\d+)/conversations)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             auto authenticatedUserId = authenticatedUserId_(req);
             if (!authenticatedUserId.has_value()) {
               res.status = httplib::Unauthorized_401;
               res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                               content_types::Json);
               return;
             }
             uint64_t userId = std::stoull(std::string(req.matches[1]));
             if (*authenticatedUserId != userId) {
               res.status = httplib::Forbidden_403;
               res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(),
                               content_types::Json);
               return;
             }
             spdlog::info("Fetching conversations for user: {}", userId);
             auto convs = db_.getConversationsByUserId(userId);
             nlohmann::json result = convs;
             res.set_content(result.dump(), content_types::Json);
           });
}

void Controller::messageController_() {
  svr_.Post(R"(/conversations/(\d+)/messages)", [this](const httplib::Request& req,
                                                       httplib::Response& res) {
    uint64_t convId = std::stoull(std::string(req.matches[1]));

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains(json_fields::SenderId) ||
        !body.contains(json_fields::Payload)) {
      res.status = httplib::BadRequest_400;
      res.set_content(nlohmann::json{{json_fields::Error, "senderId and payload required"}}.dump(),
                      content_types::Json);
      return;
    }

    uint64_t senderId = body[json_fields::SenderId];
    std::string payload = body[json_fields::Payload];
    std::optional<uint64_t> recipientId;
    if (body.contains(json_fields::RecipientId) && !body[json_fields::RecipientId].is_null()) {
      recipientId = body[json_fields::RecipientId].get<uint64_t>();
    }

    if (payload.empty() || payload.size() > 65536) {
      res.status = httplib::BadRequest_400;
      res.set_content(
          nlohmann::json{{json_fields::Error, "payload must be 1–65536 characters"}}.dump(),
          content_types::Json);
      return;
    }

    auto authenticatedUserId = authenticatedUserId_(req);
    if (!authenticatedUserId.has_value()) {
      res.status = httplib::Unauthorized_401;
      res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                      content_types::Json);
      return;
    }
    if (*authenticatedUserId != senderId) {
      res.status = httplib::Forbidden_403;
      res.set_content(
          nlohmann::json{{json_fields::Error, "session user cannot send as another user"}}.dump(),
          content_types::Json);
      return;
    }
    if (!db_.isParticipant(convId, *authenticatedUserId)) {
      res.status = httplib::Forbidden_403;
      res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(),
                      content_types::Json);
      return;
    }
    if (recipientId.has_value() && !db_.isParticipant(convId, *recipientId)) {
      res.status = httplib::BadRequest_400;
      res.set_content(nlohmann::json{{json_fields::Error,
                                      "recipient must be a conversation participant"}}
                          .dump(),
                      content_types::Json);
      return;
    }

    auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());

    auto oneTimePreKeyId = oneTimePreKeyIdFromPayload(payload);
    uint64_t msgId = 0;
    try {
      msgId = db_.createMessage(convId, senderId, payload, now, recipientId, oneTimePreKeyId);
    } catch (const std::runtime_error& e) {
      res.status = httplib::Conflict_409;
      res.set_content(nlohmann::json{{json_fields::Error, e.what()}}.dump(), content_types::Json);
      return;
    }
    spdlog::info("Created message {} in conversation {}", msgId, convId);

    // Record hash on-chain via the blockchain sidecar
    std::string txHash;
    try {
      httplib::Client sidecar(sidecarUrl_);
      sidecar.set_connection_timeout(30);
      sidecar.set_read_timeout(60);
      nlohmann::json sidecarBody = {{json_fields::ConversationId, convId},
                                    {json_fields::MsgId, msgId},
                                    {json_fields::Ciphertext, payload}};
      auto sidecarRes =
          sidecar.Post(routes::SidecarRecord, sidecarBody.dump(), content_types::Json);
      if (sidecarRes && sidecarRes->status == 200) {
        auto sidecarJson = nlohmann::json::parse(sidecarRes->body);
        txHash = sidecarJson[json_fields::TxHash].get<std::string>();
        db_.updateMessageBlockchainDigest(msgId, txHash);
        spdlog::info("Blockchain digest recorded for message {}: {}", msgId, txHash);
      } else {
        spdlog::warn("Blockchain sidecar unavailable for message {}", msgId);
      }
    } catch (const std::exception& e) {
      spdlog::warn("Blockchain sidecar error for message {}: {}", msgId, e.what());
    }

    kd::Message msg{msgId, senderId, convId, payload, now, txHash};
    nlohmann::json result = msg;
    res.status = httplib::Created_201;
    res.set_content(result.dump(), content_types::Json);
  });

  svr_.Delete(R"(/conversations/(\d+)/messages/(\d+))",
              [this](const httplib::Request& req, httplib::Response& res) -> void {
                auto authenticatedUserId = authenticatedUserId_(req);
                if (!authenticatedUserId.has_value()) {
                  res.status = httplib::Unauthorized_401;
                  res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                                  content_types::Json);
                  return;
                }

                uint64_t convId = std::stoull(std::string(req.matches[1]));
                uint64_t messageId = std::stoull(std::string(req.matches[2]));
                if (!db_.isParticipant(convId, *authenticatedUserId)) {
                  res.status = httplib::Forbidden_403;
                  res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(),
                                  content_types::Json);
                  return;
                }

                if (!db_.deleteMessage(convId, messageId, *authenticatedUserId)) {
                  res.status = httplib::NotFound_404;
                  res.set_content(nlohmann::json{{json_fields::Error, "message not found"}}.dump(),
                                  content_types::Json);
                  return;
                }

                res.set_content(nlohmann::json{{json_fields::Status, "deleted"},
                                               {json_fields::MessageId, messageId}}
                                    .dump(),
                                content_types::Json);
              });

  svr_.Delete(
      R"(/conversations/(\d+)/messages/(\d+)/access/(\d+))",
      [this](const httplib::Request& req, httplib::Response& res) -> void {
        auto authenticatedUserId = authenticatedUserId_(req);
        if (!authenticatedUserId.has_value()) {
          res.status = httplib::Unauthorized_401;
          res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                          content_types::Json);
          return;
        }

        uint64_t convId = std::stoull(std::string(req.matches[1]));
        uint64_t messageId = std::stoull(std::string(req.matches[2]));
        uint64_t targetUserId = std::stoull(std::string(req.matches[3]));
        if (!db_.isParticipant(convId, *authenticatedUserId) ||
            !db_.isParticipant(convId, targetUserId)) {
          res.status = httplib::Forbidden_403;
          res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(),
                          content_types::Json);
          return;
        }

        auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());
        if (!db_.revokeMessageAccess(convId, messageId, *authenticatedUserId, targetUserId, now)) {
          res.status = httplib::NotFound_404;
          res.set_content(nlohmann::json{{json_fields::Error, "message access not found"}}.dump(),
                          content_types::Json);
          return;
        }

        res.set_content(nlohmann::json{{json_fields::Status, "revoked"},
                                       {json_fields::MessageId, messageId},
                                       {json_fields::UserId, targetUserId}}
                            .dump(),
                        content_types::Json);
      });

  svr_.Get(R"(/conversations/(\d+)/messages)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             auto authenticatedUserId = authenticatedUserId_(req);
             if (!authenticatedUserId.has_value()) {
               res.status = httplib::Unauthorized_401;
               res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                               content_types::Json);
               return;
             }
             uint64_t convId = std::stoull(std::string(req.matches[1]));
             if (!db_.isParticipant(convId, *authenticatedUserId)) {
               res.status = httplib::Forbidden_403;
               res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(),
                               content_types::Json);
               return;
             }
             auto msgs = db_.getMessagesByConversationIdForUser(convId, *authenticatedUserId);
             nlohmann::json result = msgs;
             res.set_content(result.dump(), content_types::Json);
           });
}

void Controller::basicApiInfo_() {
  svr_.Get(routes::Api, [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json info = {{json_fields::Name, api_info::Name},
                           {json_fields::Version, api_info::Version}};
    res.set_content(info.dump(), content_types::Json);
  });
}

void Controller::notFoundHandler_() {
  svr_.set_error_handler([](const httplib::Request&, httplib::Response& res) -> void {
    if (res.status == httplib::NotFound_404) {
      nlohmann::json error = {{json_fields::Error, "Not Found"},
                              {json_fields::Status, httplib::NotFound_404}};
      res.set_content(error.dump(), content_types::Json);
    }
  });
}

}  // namespace kd
