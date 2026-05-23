#include "Controller.hh"

#include <httplib.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sodium.h>

#include <chrono>
#include <cstdint>
#include <kd/Conversation.hpp>
#include <kd/Message.hpp>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace kd {

namespace {

std::string hashPassword(const std::string& password) {
  char encodedHash[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str_alg(encodedHash, password.c_str(), password.size(),
                            crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                            crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Failed to hash password");
  }
  return encodedHash;
}

bool verifyPassword(const std::string& encodedHash, const std::string& password) {
  return crypto_pwhash_str_verify(encodedHash.c_str(), password.c_str(), password.size()) == 0;
}

std::string base64UrlEncode(const unsigned char* data, size_t size) {
  std::string encoded(4 * ((size + 2) / 3), '\0');
  const int encodedSize = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), data,
                                          static_cast<int>(size));
  encoded.resize(encodedSize);

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

std::string base64UrlEncode(const std::string& value) {
  return base64UrlEncode(reinterpret_cast<const unsigned char*>(value.data()), value.size());
}

std::string signJwtInput(const std::string& signingInput, const std::string& secret) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLength = 0;
  HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
       reinterpret_cast<const unsigned char*>(signingInput.data()), signingInput.size(), digest,
       &digestLength);
  return base64UrlEncode(digest, digestLength);
}

uint64_t epochSeconds() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
}

std::optional<std::string> bearerToken(const httplib::Request& req) {
  const std::string prefix = "Bearer ";
  auto authorization = req.get_header_value("Authorization");
  if (authorization.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  auto token = authorization.substr(prefix.size());
  if (token.empty()) {
    return std::nullopt;
  }
  return token;
}

std::optional<nlohmann::json> verifiedJwtPayload(const std::string& token,
                                                 const std::string& secret) {
  auto firstDot = token.find('.');
  if (firstDot == std::string::npos) {
    return std::nullopt;
  }
  auto secondDot = token.find('.', firstDot + 1);
  if (secondDot == std::string::npos || token.find('.', secondDot + 1) != std::string::npos) {
    return std::nullopt;
  }

  auto signingInput = token.substr(0, secondDot);
  auto signature = token.substr(secondDot + 1);
  auto expectedSignature = signJwtInput(signingInput, secret);
  if (signature.size() != expectedSignature.size() ||
      CRYPTO_memcmp(signature.data(), expectedSignature.data(), signature.size()) != 0) {
    return std::nullopt;
  }

  auto payloadPart = token.substr(firstDot + 1, secondDot - firstDot - 1);
  for (char& ch : payloadPart) {
    if (ch == '-') {
      ch = '+';
    } else if (ch == '_') {
      ch = '/';
    }
  }
  while (payloadPart.size() % 4 != 0) {
    payloadPart.push_back('=');
  }

  std::vector<unsigned char> decoded(3 * (payloadPart.size() / 4) + 3);
  auto decodedSize =
      EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char*>(payloadPart.data()),
                      static_cast<int>(payloadPart.size()));
  if (decodedSize < 0) {
    return std::nullopt;
  }
  while (!payloadPart.empty() && payloadPart.back() == '=') {
    --decodedSize;
    payloadPart.pop_back();
  }

  auto payload =
      nlohmann::json::parse(std::string(reinterpret_cast<char*>(decoded.data()), decodedSize),
                            nullptr, false);
  if (payload.is_discarded() || !payload.contains("sub") || !payload.contains("exp")) {
    return std::nullopt;
  }
  if (!payload["exp"].is_number_unsigned() || payload["exp"].get<uint64_t>() <= epochSeconds()) {
    return std::nullopt;
  }
  return payload;
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
  std::vector<std::string> securityPredicates = {"ValidateSenderAuthenticity", "ValidateUntampered",
                                                 "ValidateAuthenticated"};
  securityFilterChain_ = std::make_unique<SecurityFilterChain>(securityPredicates);

  setupRoutes();
}

void Controller::setupRoutes() {
  svr_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    auto error = securityFilterChain_->Execute(req);
    if (error.has_value()) {
      nlohmann::json errorJson = {{"error", error->message}};
      res.status = error->httpStatusCode;
      res.set_content(errorJson.dump(), "application/json");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  notFoundHandler_();
  healthController_();
  authController_();
  publicKeyController_();
  basicApiInfo_();

  // conversations
  conversationController_();
  messageController_();
}

std::string Controller::createSession_(uint64_t userId, const std::string& username) const {
  const auto issuedAt = epochSeconds();
  nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
  nlohmann::json payload = {{"sub", std::to_string(userId)},
                            {"username", username},
                            {"iat", issuedAt},
                            {"exp", issuedAt + jwtTtlSeconds_}};

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
    return std::stoull((*payload)["sub"].get<std::string>());
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void Controller::authController_() {
  svr_.Post("/signup", [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Sign-up request received");

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("username") || !body.contains("password") ||
        !body.contains("publicKey")) {
      res.status = 400;
      res.set_content(
          nlohmann::json{{"error", "username, password, and publicKey required"}}.dump(),
          "application/json");
      return;
    }

    std::string username = body["username"];
    std::string password = body["password"];
    std::string publicKey = body["publicKey"];

    try {
      auto passwordHash = hashPassword(password);
      uint64_t id = db_.createUser(username, passwordHash, publicKey);
      auto sessionToken = createSession_(id, username);
      spdlog::info("Created user '{}' with id {}", username, id);
      res.status = 201;
      res.set_content(nlohmann::json{{"id", id},
                                     {"username", username},
                                     {"publicKey", publicKey},
                                     {"token", sessionToken},
                                     {"sessionToken", sessionToken}}
                          .dump(),
                      "application/json");
    } catch (const std::runtime_error& e) {
      res.status = 409;
      res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
    }
  });

  // Login endpoint
  svr_.Post("/login", [this](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Login request received: {}", req.body);
    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("username") || !body.contains("password")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "username and password required"}}.dump(),
                      "application/json");
      return;
    }

    std::string username = body["username"];
    std::string password = body["password"];
    auto user = db_.getUserByUsername(username);
    if (!user.has_value() || !verifyPassword(user->passwordHash, password)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"error", "invalid username or password"}}.dump(),
                      "application/json");
      return;
    }

    auto sessionToken = createSession_(user->id, user->username);
    res.set_content(nlohmann::json{{"id", user->id},
                                   {"username", user->username},
                                   {"token", sessionToken},
                                   {"sessionToken", sessionToken}}
                        .dump(),
                    "application/json");
  });

  // Logout endpoint
  svr_.Post("/logout", [](const httplib::Request& req, httplib::Response& res) {
    spdlog::info("Logout request received: {}", req.body);
    nlohmann::json response = {{"status", "success"}, {"message", "Token cleared client-side"}};
    res.set_content(response.dump(), "application/json");
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
  svr_.Get("/health", [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json status = {{"status", "ok"}};
    res.set_content(status.dump(), "application/json");
  });
}

void Controller::publicKeyController_() {
  svr_.Get(R"(/users/(\d+)/public-key)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             uint64_t userId = std::stoull(std::string(req.matches[1]));
             auto publicKey = db_.getUserPublicKey(userId);
             if (!publicKey.has_value()) {
               res.status = 404;
               res.set_content(nlohmann::json{{"error", "user not found"}}.dump(),
                               "application/json");
               return;
             }
             res.set_content(nlohmann::json{{"userId", userId}, {"publicKey", *publicKey}}.dump(),
                             "application/json");
           });
}

void Controller::conversationController_() {
  svr_.Post("/conversations", [this](const httplib::Request& req, httplib::Response& res) {
    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("name") || !body.contains("participantIds")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "name and participantIds required"}}.dump(),
                      "application/json");
      return;
    }

    std::string name = body["name"];
    auto participantIds = body["participantIds"].get<std::vector<uint64_t>>();

    uint64_t id = db_.createConversation(name, participantIds);
    spdlog::info("Created conversation '{}' with id {}", name, id);
    res.status = 201;
    res.set_content(nlohmann::json{{"id", id}, {"name", name}}.dump(), "application/json");
  });

  svr_.Get(R"(/users/(\d+)/conversations)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             uint64_t userId = std::stoull(std::string(req.matches[1]));
             spdlog::info("Fetching conversations for user: {}", userId);
             auto convs = db_.getConversationsByUserId(userId);
             nlohmann::json result = convs;
             res.set_content(result.dump(), "application/json");
           });
}

void Controller::messageController_() {
  svr_.Post(R"(/conversations/(\d+)/messages)", [this](const httplib::Request& req,
                                                       httplib::Response& res) {
    uint64_t convId = std::stoull(std::string(req.matches[1]));

    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.contains("senderId") || !body.contains("payload")) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "senderId and payload required"}}.dump(),
                      "application/json");
      return;
    }

    uint64_t senderId = body["senderId"];
    std::string payload = body["payload"];
    auto authenticatedUserId = authenticatedUserId_(req);
    if (!authenticatedUserId.has_value()) {
      res.status = 401;
      res.set_content(nlohmann::json{{"error", "login required"}}.dump(), "application/json");
      return;
    }
    if (*authenticatedUserId != senderId) {
      res.status = 403;
      res.set_content(nlohmann::json{{"error", "session user cannot send as another user"}}.dump(),
                      "application/json");
      return;
    }

    auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());

    uint64_t msgId = db_.createMessage(convId, senderId, payload, now);
    spdlog::info("Created message {} in conversation {}", msgId, convId);

    // Record hash on-chain via the blockchain sidecar
    std::string txHash;
    try {
      httplib::Client sidecar(sidecarUrl_);
      sidecar.set_connection_timeout(30);
      sidecar.set_read_timeout(60);
      nlohmann::json sidecarBody = {{"conversationId", convId}, {"ciphertext", payload}};
      auto sidecarRes = sidecar.Post("/record", sidecarBody.dump(), "application/json");
      if (sidecarRes && sidecarRes->status == 200) {
        auto sidecarJson = nlohmann::json::parse(sidecarRes->body);
        txHash = sidecarJson["txHash"].get<std::string>();
        db_.updateMessageBlockchainDigest(msgId, txHash);
        spdlog::info("Blockchain digest recorded for message {}: {}", msgId, txHash);
      } else {
        spdlog::warn("Blockchain sidecar unavailable for message {}", msgId);
      }
    } catch (const std::exception& e) {
      spdlog::warn("Blockchain sidecar error for message {}: {}", msgId, e.what());
    }

    kd::Message msg{msgId, senderId, convId, payload, "", now, txHash};
    nlohmann::json result = msg;
    res.status = 201;
    res.set_content(result.dump(), "application/json");
  });

  svr_.Get(R"(/conversations/(\d+)/messages)",
           [this](const httplib::Request& req, httplib::Response& res) -> void {
             uint64_t convId = std::stoull(std::string(req.matches[1]));
             auto msgs = db_.getMessagesByConversationId(convId);
             nlohmann::json result = msgs;
             res.set_content(result.dump(), "application/json");
           });
}

void Controller::basicApiInfo_() {
  svr_.Get("/", [](const httplib::Request&, httplib::Response& res) -> void {
    nlohmann::json info = {{"name", "Kingdom Server"}, {"version", "1.0"}};
    res.set_content(info.dump(), "application/json");
  });
}

void Controller::notFoundHandler_() {
  svr_.set_error_handler([](const httplib::Request&, httplib::Response& res) -> void {
    if (res.status == httplib::NotFound_404) {
      nlohmann::json error = {{"error", "Not Found"}, {"status", httplib::NotFound_404}};
      res.set_content(error.dump(), "application/json");
    }
  });
}

}  // namespace kd
