#include "Controller.hh"

#include <sodium.h>

#include <cctype>
#include <charconv>
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
constexpr const char* EntityId = "id";
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
constexpr const char* PendingId = "pendingId";
constexpr const char* Typ = "typ";
constexpr const char* UserId = "userId";
constexpr const char* Username = "username";
constexpr const char* Version = "version";
}  // namespace json_fields

namespace api_info {
constexpr const char* Name = "Kingdom Server";
constexpr const char* Version = "1.0";
}  // namespace api_info

std::optional<uint64_t> parseId(std::string_view s) noexcept {
  uint64_t val{};
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  return (ec == std::errc{} && ptr == s.data() + s.size()) ? std::optional{val} : std::nullopt;
}

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
  if (password.size() < domain::kMinPasswordLen || password.size() > domain::kMaxPasswordLen) {
    return false;
  }

  bool hasUppercase = false;
  bool hasNumber = false;
  for (unsigned char chr : password) {
    hasUppercase = hasUppercase || std::isupper(chr) != 0;
    hasNumber = hasNumber || std::isdigit(chr) != 0;
  }

  return hasUppercase && hasNumber;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Controller::Controller(std::string host, int port, const std::string& dbConnectionString,
                       std::string sidecarUrl, const std::string& certPath,
                       // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                       const std::string& keyPath, std::string jwtSecret, uint64_t jwtTtlSeconds,
                       int rateLimitMaxRequests)
    : host_(std::move(host))
    , port_(port)
    , sidecarUrl_(std::move(sidecarUrl))
    , svr_(certPath.c_str(), keyPath.c_str())
    , db_(dbConnectionString)
    , jwtSecret_(std::move(jwtSecret))
    , jwtTtlSeconds_(jwtTtlSeconds)
    , rateLimitMaxRequests_(rateLimitMaxRequests) {
  if (sodium_init() < 0) {
    throw std::runtime_error("Failed to initialize libsodium");
  }
  std::vector<std::string> securityPredicates = {security_predicates::ValidateSenderAuthenticity,
                                                 security_predicates::ValidateUntampered,
                                                 security_predicates::ValidateAuthenticated};
  securityFilterChain_ = std::make_unique<SecurityFilterChain>(securityPredicates);

  setupRoutes();
  startBlockchainResolver_();
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

  registerNotFoundHandler_();
  healthController_();
  registerAuthRoutes_();
  userController_();
  publicKeyController_();
  basicApiInfo_();

  // conversations
  registerConversationRoutes_();
  registerMessageRoutes_();
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

bool Controller::isRateLimited_(const std::string& ipAddr) {
  constexpr auto kWindow = std::chrono::seconds(timeouts::kRateLimitWindowSec);

  std::scoped_lock lock(rateLimitMutex_);
  auto now = std::chrono::steady_clock::now();
  auto& entry = rateLimitMap_[ipAddr];

  if (now - entry.windowStart >= kWindow) {
    entry.count = 0;
    entry.windowStart = now;
  }

  ++entry.count;
  return entry.count > rateLimitMaxRequests_;
}

void Controller::registerAuthRoutes_() {
  svr_.Post(routes::Signup, [this](const httplib::Request& req, httplib::Response& res) {
    handleSignup_(req, res);
  });

  svr_.Post(routes::Login, [this](const httplib::Request& req, httplib::Response& res) {
    handleLogin_(req, res);
  });

  svr_.Post(routes::Logout, [](const httplib::Request& req, httplib::Response& res) {
    Controller::handleLogout_(req, res);
  });
}

void Controller::handleSignup_(const httplib::Request& req, httplib::Response& res) {
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

  if (username.empty() || username.size() > domain::kMaxUsernameLen) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "username must be 1–64 characters"}}.dump(),
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
  if (publicKey.empty() || publicKey.size() > domain::kMaxPublicKeyLen) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "publicKey must be 1–8192 characters"}}.dump(),
                    content_types::Json);
    return;
  }

  try {
    auto passwordHash = User::hashPassword(password);
    auto newUserId = db_.createUser(username, passwordHash, publicKey);
    auto sessionToken = createSession_(newUserId, username);
    spdlog::info("Created user '{}' with id {}", username, newUserId);
    res.status = httplib::Created_201;
    res.set_content(nlohmann::json{{json_fields::EntityId, newUserId},
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
}

void Controller::handleLogin_(const httplib::Request& req, httplib::Response& res) {
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

  if (username.empty() || username.size() > domain::kMaxUsernameLen) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "username must be 1–64 characters"}}.dump(),
                    content_types::Json);
    return;
  }
  if (password.empty() || password.size() > domain::kMaxPasswordLen) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "password must be 1–72 characters"}}.dump(),
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
  res.set_content(nlohmann::json{{json_fields::EntityId, user->id()},
                                 {json_fields::Username, user->username()},
                                 {json_fields::Token, sessionToken},
                                 {json_fields::SessionToken, sessionToken}}
                      .dump(),
                  content_types::Json);
}

void Controller::handleLogout_(const httplib::Request& req, httplib::Response& res) {
  spdlog::info("Logout request received");
  auto token = bearerToken(req);
  if (token.has_value()) {
    TokenBlacklist::revoke(*token);
    spdlog::info("Token revoked");
  }
  nlohmann::json response = {{json_fields::Status, "success"},
                             {json_fields::Message, "Logged out"}};
  res.set_content(response.dump(), content_types::Json);
}

Controller::~Controller() {
  stopBlockchainResolver_ = true;
  if (blockchainResolverThread_.joinable()) {
    blockchainResolverThread_.join();
  }
}

void Controller::startBlockchainResolver_() {
  blockchainResolverThread_ = std::thread([this]() {
    while (!stopBlockchainResolver_) {
      // Sleep in 1-second increments so shutdown is prompt.
      for (int i = 0; i < timeouts::kBlockchainResolverIntervalSec && !stopBlockchainResolver_;
           ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      if (stopBlockchainResolver_) {
        break;
      }

      auto pending = db_.getPendingBlockchainMessages();
      if (pending.empty()) {
        continue;
      }

      spdlog::info("Blockchain resolver: checking {} pending message(s)", pending.size());

      httplib::Client sidecar(sidecarUrl_);
      sidecar.set_connection_timeout(timeouts::kSidecarConnectionTimeoutSec);
      sidecar.set_read_timeout(timeouts::kSidecarReadTimeoutSec);

      for (const auto& [msgId, pendingId] : pending) {
        try {
          auto sidecarRes = sidecar.Get("/pending/" + pendingId);
          if (!sidecarRes || sidecarRes->status != httplib::OK_200) {
            continue;
          }
          auto json = nlohmann::json::parse(sidecarRes->body);
          std::string resolvedStatus = json[json_fields::Status].get<std::string>();

          if (resolvedStatus == "confirmed") {
            auto txHash = json[json_fields::TxHash].get<std::string>();
            db_.updateMessageBlockchainDigest(msgId, txHash);
            spdlog::info("Blockchain confirmed for message {}: {}", msgId, txHash);
          } else if (resolvedStatus == "failed") {
            db_.updateMessageBlockchainDigest(msgId, "");
            spdlog::warn("Blockchain batch failed for message {} — digest cleared", msgId);
          }
        } catch (const std::exception& e) {
          spdlog::warn("Blockchain resolver error for message {}: {}", msgId, e.what());
        }
      }
    }
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
    for (const auto& user : users) {
      arr.push_back({{json_fields::EntityId, user.id()}, {json_fields::Username, user.username()}});
    }
    res.set_content(arr.dump(), content_types::Json);
  });
}

void Controller::publicKeyController_() {
  svr_.Get(routes::UserPublicKey,
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             auto userId = parseId(req.matches[1].str());
             if (!userId) {
               res.status = httplib::BadRequest_400;
               res.set_content(
                   nlohmann::json{{json_fields::Error, "invalid user id"}}.dump(),
                   content_types::Json);
               return;
             }
             auto publicKey = db_.getUserPublicKey(*userId);
             if (!publicKey.has_value()) {
               res.status = httplib::NotFound_404;
               res.set_content(nlohmann::json{{json_fields::Error, "user not found"}}.dump(),
                               content_types::Json);
               return;
             }
             res.set_content(nlohmann::json{{json_fields::UserId, *userId},
                                            {json_fields::PublicKey, *publicKey}}
                                 .dump(),
                             content_types::Json);
           });
}

void Controller::registerConversationRoutes_() {
  svr_.Post("/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    handleCreateConversation_(req, res);
  });

  svr_.Get(R"(/users/(\d+)/conversations)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             handleListUserConversations_(req, res);
           });
}

void Controller::handleCreateConversation_(const httplib::Request& req, httplib::Response& res) {
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
    res.set_content(nlohmann::json{{json_fields::Error, "name and participantIds required"}}.dump(),
                    content_types::Json);
    return;
  }

  std::string name = body[json_fields::Name];

  if (name.empty() || name.size() > domain::kMaxConversationNameLen) {
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

  auto newConvId = db_.createConversation(name, participantIds);
  spdlog::info("Created conversation '{}' with id {}", name, newConvId);
  res.status = httplib::Created_201;
  res.set_content(
      nlohmann::json{{json_fields::EntityId, newConvId}, {json_fields::Name, name}}.dump(),
      content_types::Json);
}

void Controller::handleListUserConversations_(const httplib::Request& req, httplib::Response& res) {
  auto authenticatedUserId = authenticatedUserId_(req);
  if (!authenticatedUserId.has_value()) {
    res.status = httplib::Unauthorized_401;
    res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                    content_types::Json);
    return;
  }
  auto userId = parseId(req.matches[1].str());
  if (!userId) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "invalid user id"}}.dump(),
                    content_types::Json);
    return;
  }
  if (*authenticatedUserId != *userId) {
    res.status = httplib::Forbidden_403;
    res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(), content_types::Json);
    return;
  }
  spdlog::info("Fetching conversations for user: {}", *userId);
  auto convs = db_.getConversationsByUserId(*userId);
  nlohmann::json result = convs;
  res.set_content(result.dump(), content_types::Json);
}

void Controller::registerMessageRoutes_() {
  svr_.Post(R"(/conversations/(\d+)/messages)",
            [this](const httplib::Request& req, httplib::Response& res) {
              handleCreateMessage_(req, res);
            });

  svr_.Delete(R"(/conversations/(\d+)/messages/(\d+))",
              [this](const httplib::Request& req, httplib::Response& res) -> void {
                handleDeleteMessage_(req, res);
              });

  svr_.Delete(R"(/conversations/(\d+)/messages/(\d+)/access/(\d+))",
              [this](const httplib::Request& req, httplib::Response& res) -> void {
                handleRevokeMessageAccess_(req, res);
              });

  svr_.Get(R"(/conversations/(\d+)/messages)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             handleListConversationMessages_(req, res);
           });
}

void Controller::handleCreateMessage_(const httplib::Request& req, httplib::Response& res) {
  auto convId = parseId(req.matches[1].str());
  if (!convId) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "invalid conversation id"}}.dump(),
                    content_types::Json);
    return;
  }

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

  if (payload.empty() || payload.size() > domain::kMaxPayloadLen) {
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
  if (!db_.isParticipant(*convId, *authenticatedUserId)) {
    res.status = httplib::Forbidden_403;
    res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(), content_types::Json);
    return;
  }
  if (recipientId.has_value() && !db_.isParticipant(*convId, *recipientId)) {
    res.status = httplib::BadRequest_400;
    res.set_content(
        nlohmann::json{{json_fields::Error, "recipient must be a conversation participant"}}.dump(),
        content_types::Json);
    return;
  }

  auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());

  auto oneTimePreKeyId = oneTimePreKeyIdFromPayload(payload);
  uint64_t msgId = 0;
  try {
    msgId = db_.createMessage(*convId, senderId, payload, now, recipientId, oneTimePreKeyId);
  } catch (const std::runtime_error& e) {
    res.status = httplib::Conflict_409;
    res.set_content(nlohmann::json{{json_fields::Error, e.what()}}.dump(), content_types::Json);
    return;
  }
  spdlog::info("Created message {} in conversation {}", msgId, *convId);

  // Queue message for the next blockchain batch via the sidecar.
  std::string blockchainDigest;
  try {
    httplib::Client sidecar(sidecarUrl_);
    sidecar.set_connection_timeout(timeouts::kSidecarConnectionTimeoutSec);
    sidecar.set_read_timeout(timeouts::kSidecarReadTimeoutSec);
    nlohmann::json sidecarBody = {{json_fields::ConversationId, *convId},
                                  {json_fields::MsgId, msgId},
                                  {json_fields::Ciphertext, payload}};
    auto sidecarRes = sidecar.Post(routes::SidecarRecord, sidecarBody.dump(), content_types::Json);
    if (sidecarRes && sidecarRes->status == httplib::OK_200) {
      auto sidecarJson = nlohmann::json::parse(sidecarRes->body);
      auto pendingId = sidecarJson[json_fields::PendingId].get<std::string>();
      blockchainDigest = std::string(blockchain::kPendingPrefix) + pendingId;
      db_.updateMessageBlockchainDigest(msgId, blockchainDigest);
      spdlog::info("Message {} queued for blockchain batch. pendingId={}", msgId, pendingId);
    } else {
      spdlog::warn("Blockchain sidecar unavailable for message {}", msgId);
    }
  } catch (const std::exception& e) {
    spdlog::warn("Blockchain sidecar error for message {}: {}", msgId, e.what());
  }

  kd::Message msg{.id = msgId,
                  .senderId = senderId,
                  .conversationId = *convId,
                  .payload = payload,
                  .timestamp = now,
                  .blockchainDigest = blockchainDigest};
  nlohmann::json result = msg;
  res.status = httplib::Created_201;
  res.set_content(result.dump(), content_types::Json);
}

void Controller::handleDeleteMessage_(const httplib::Request& req, httplib::Response& res) {
  auto authenticatedUserId = authenticatedUserId_(req);
  if (!authenticatedUserId.has_value()) {
    res.status = httplib::Unauthorized_401;
    res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                    content_types::Json);
    return;
  }

  auto convId = parseId(req.matches[1].str());
  auto messageId = parseId(req.matches[2].str());
  if (!convId || !messageId) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "invalid id"}}.dump(), content_types::Json);
    return;
  }
  if (!db_.isParticipant(*convId, *authenticatedUserId)) {
    res.status = httplib::Forbidden_403;
    res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(), content_types::Json);
    return;
  }

  if (!db_.deleteMessage(*convId, *messageId, *authenticatedUserId)) {
    res.status = httplib::NotFound_404;
    res.set_content(nlohmann::json{{json_fields::Error, "message not found"}}.dump(),
                    content_types::Json);
    return;
  }

  res.set_content(
      nlohmann::json{{json_fields::Status, "deleted"}, {json_fields::MessageId, *messageId}}.dump(),
      content_types::Json);
}

void Controller::handleRevokeMessageAccess_(const httplib::Request& req, httplib::Response& res) {
  auto authenticatedUserId = authenticatedUserId_(req);
  if (!authenticatedUserId.has_value()) {
    res.status = httplib::Unauthorized_401;
    res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                    content_types::Json);
    return;
  }

  auto convId = parseId(req.matches[1].str());
  auto messageId = parseId(req.matches[2].str());
  auto targetUserId = parseId(req.matches[3].str());
  if (!convId || !messageId || !targetUserId) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "invalid id"}}.dump(), content_types::Json);
    return;
  }
  if (!db_.isParticipant(*convId, *authenticatedUserId) ||
      !db_.isParticipant(*convId, *targetUserId)) {
    res.status = httplib::Forbidden_403;
    res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(), content_types::Json);
    return;
  }

  auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
  if (!db_.revokeMessageAccess(*convId, *messageId, *authenticatedUserId, *targetUserId, now)) {
    res.status = httplib::NotFound_404;
    res.set_content(nlohmann::json{{json_fields::Error, "message access not found"}}.dump(),
                    content_types::Json);
    return;
  }

  res.set_content(nlohmann::json{{json_fields::Status, "revoked"},
                                 {json_fields::MessageId, *messageId},
                                 {json_fields::UserId, *targetUserId}}
                      .dump(),
                  content_types::Json);
}

void Controller::handleListConversationMessages_(const httplib::Request& req,
                                                 httplib::Response& res) {
  auto authenticatedUserId = authenticatedUserId_(req);
  if (!authenticatedUserId.has_value()) {
    res.status = httplib::Unauthorized_401;
    res.set_content(nlohmann::json{{json_fields::Error, "login required"}}.dump(),
                    content_types::Json);
    return;
  }
  auto convId = parseId(req.matches[1].str());
  if (!convId) {
    res.status = httplib::BadRequest_400;
    res.set_content(nlohmann::json{{json_fields::Error, "invalid conversation id"}}.dump(),
                    content_types::Json);
    return;
  }
  if (!db_.isParticipant(*convId, *authenticatedUserId)) {
    res.status = httplib::Forbidden_403;
    res.set_content(nlohmann::json{{json_fields::Error, "forbidden"}}.dump(), content_types::Json);
    return;
  }
  auto msgs = db_.getMessagesByConversationIdForUser(*convId, *authenticatedUserId);
  nlohmann::json result = msgs;
  res.set_content(result.dump(), content_types::Json);
}

void Controller::basicApiInfo_() {
  svr_.Get(routes::Api, [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json info = {{json_fields::Name, api_info::Name},
                           {json_fields::Version, api_info::Version}};
    res.set_content(info.dump(), content_types::Json);
  });
}

void Controller::registerNotFoundHandler_() {
  svr_.set_error_handler([](const httplib::Request&, httplib::Response& res) -> void {
    if (res.status == httplib::NotFound_404) {
      nlohmann::json error = {{json_fields::Error, "Not Found"},
                              {json_fields::Status, httplib::NotFound_404}};
      res.set_content(error.dump(), content_types::Json);
    }
  });
}

}  // namespace kd
