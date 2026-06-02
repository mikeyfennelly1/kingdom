# Network Architecture Documentation — Kingdom

CS4455 Cybersecurity | Computer Networks & Cybersecurity (Mark Burkley) | 2026

---

## 1. System Architecture Diagram

```
  User Machine
  ┌──────────────────────────────────────────────────────────────────────┐
  │                                                                      │
  │  ┌────────────────────────────────────────────────────────────────┐  │
  │  │  kdctl  (C++20 Qt6 GUI client)                                 │  │
  │  │                                                                │  │
  │  │  LoginWindow / MainWindow (Qt6 widgets)                        │  │
  │  │  kd::Client — cpp-httplib SSLClient, cert verification on      │  │
  │  │  kd::LocalKeyStore — X3DH keys, XChaCha20-Poly1305 E2EE       │  │
  │  │  kd::MessageStore — encrypted local plaintext cache            │  │
  │  │                                                                │  │
  │  │  ~/.kingdom/keys/<user>.json       (enc. identity key bundle)  │  │
  │  │  ~/.kingdom/keys/known_keys.json   (TOFU pinning store)        │  │
  │  │  ~/.kingdom/messages/<user>.json   (enc. message cache)        │  │
  │  └──────────────────────────────┬─────────────────────────────────┘  │
  │                                 │  HTTPS / TLS  (port 8080)          │
  └─────────────────────────────────┼────────────────────────────────────┘
                                    │
  VM  (200.69.13.70, port 4000 → internal 8080 via port-forward)
  ┌─────────────────────────────────▼────────────────────────────────────┐
  │                                                                      │
  │  ┌───────────────────────────────────────────────────────────────┐   │
  │  │  kds  (C++20 HTTPS server — Docker container)                 │   │
  │  │                                                               │   │
  │  │  httplib::SSLServer  ·  TLS cert + key loaded from env        │   │
  │  │  SecurityFilterChain  (chain-of-responsibility):              │   │
  │  │    ValidateSenderAuthenticity · ValidateUntampered            │   │
  │  │    ValidateAuthenticated + TokenBlacklist                     │   │
  │  │  JWT HS256 session tokens  ·  per-IP rate limiting            │   │
  │  │  HSTS + X-Content-Type-Options + X-Frame-Options headers      │   │
  │  └───────────────┬──────────────────────────┬────────────────────┘   │
  │                  │  libpqxx / TCP            │  HTTP                  │
  │                  │  port 5432               │  port 3001             │
  │  ┌───────────────▼──────────────┐  ┌────────▼───────────────────┐   │
  │  │  PostgreSQL 16               │  │  Blockchain sidecar         │   │
  │  │  (Docker container)          │  │  (Node.js / Express)        │   │
  │  │                              │  │                             │   │
  │  │  users, conversations,       │  │  In-memory batch queue      │   │
  │  │  messages (ciphertext only), │  │  Merkle tree builder        │   │
  │  │  message_access              │  │  ethers.js v6               │   │
  │  └──────────────────────────────┘  └────────────┬───────────────┘   │
  │                                                  │  HTTPS / JSON-RPC  │
  └──────────────────────────────────────────────────┼────────────────────┘
                                                     │
                                       ┌─────────────▼──────────────────┐
                                       │  Ethereum Sepolia Testnet       │
                                       │                                 │
                                       │  MessageIntegrity.sol contract  │
                                       │  0x8955A2361A978A41F721e58B27   │
                                       │    66b614768Db596               │
                                       │                                 │
                                       │  recordBatch(root, leaves[])    │
                                       │  event BatchRecorded(...)       │
                                       └─────────────────────────────────┘


  Standalone (browser, no backend required):
  ┌──────────────────────────────────────────────────────────────────────┐
  │  Verification Page  (blockchain/verification/index.html)             │
  │                                                                      │
  │  1. User pastes message ciphertext + Ethereum transaction hash       │
  │  2. Browser computes keccak256(ciphertext) via ethers.js v6          │
  │  3. Page fetches BatchRecorded event from Sepolia via public RPC     │
  │     (with fallback RPC URL)                                          │
  │  4. Rebuilds Merkle root from emitted leaves                         │
  │  5. Displays VERIFIED / TAMPERED result with batch timestamp         │
  └──────────────────────────────────────────────────────────────────────┘
```

---

## 2. Components

### 2.1 kdctl — C++20 Qt6 GUI Client

`kdctl` is the user-facing client executable, built in C++20 using Qt6, cpp-httplib, and libsodium. It runs on the user's local machine and provides a two-window graphical interface: a `LoginWindow` for registration and login, and a `MainWindow` for conversation management, encrypted messaging, message forwarding, access revocation, and message deletion. A `QTimer` fires every 5 seconds to poll the server for new messages and updated conversation lists.

All cryptographic operations are handled in-process using `kd::LocalKeyStore` (X3DH key agreement, XChaCha20-Poly1305 AEAD) and `kd::MessageStore` (encrypted local plaintext cache). The client never sends plaintext to the server.

**Local storage:**
- `~/.kingdom/keys/<username>.json` — private identity key bundle (X25519, Ed25519, signed prekey, one-time prekeys), encrypted at rest under a key derived from the user's password via Argon2id + HKDF-SHA256 + XChaCha20-Poly1305. Permissions `0600`.
- `~/.kingdom/keys/known_keys.json` — TOFU public key pinning store mapping user IDs to their pinned identity key anchors. Written with permissions `0600`.
- `~/.kingdom/messages/<username>.json` — local encrypted plaintext message cache (MessageStore v2: Argon2id + HKDF-SHA256 + XChaCha20-Poly1305 envelope). Permissions `0600`.

**Outbound connections:**
- One HTTPS connection to `kds` per API call. `cpp-httplib` constructs and closes a connection per request.
- Server certificate verification is always enabled: `cli.enable_server_certificate_verification(true)`.
- A custom CA certificate path is accepted via the `KD_CA_CERT` environment variable. If not provided, the system CA store is used. If no CA certificate is available for a self-signed server certificate, the client emits a clear error message rather than silently downgrading.

**Key pinning:**
When a recipient's public key bundle is fetched from the server, `Client::getPublicKey` calls `pinPublicKey`, which reads `~/.kingdom/keys/known_keys.json`, records the identity key anchor on first encounter (Trust On First Use), and raises a runtime error on any subsequent change. This prevents the server from silently substituting a different key.

### 2.2 kds — Kingdom Data Server

`kds` is the C++20 server executable, deployed as a Docker container. It exposes a REST API over HTTPS, handles authentication, enforces access control, stores ciphertext messages in PostgreSQL, and calls the blockchain sidecar asynchronously on each message send.

**Configuration** (environment variables, loaded at startup by `configure.cc`):

| Variable | Description | Default |
|----------|-------------|---------|
| `KD_PORT` | Listening port | `8080` |
| `KD_DB_URL` | PostgreSQL connection string (libpq key=value format) | required |
| `KD_TLS_CERT` | Path to TLS certificate file | required |
| `KD_TLS_KEY` | Path to TLS private key file | required |
| `KD_JWT_SECRET` | HMAC-SHA256 secret for JWT signing (minimum 32 characters) | required |
| `KD_JWT_TTL_SECONDS` | JWT lifetime in seconds | `3600` |
| `KD_RATE_LIMIT_MAX_REQUESTS` | Maximum requests per IP per 60-second window | `10` |
| `KD_BLOCKCHAIN_SIDECAR_URL` | Sidecar base URL | `http://localhost:3001` |
| `KD_LOG_LEVEL` | spdlog log level | `info` |

**Transport:** `httplib::SSLServer` loaded with the PEM certificate and key at construction time. All API traffic is HTTPS only; no plaintext HTTP fallback is offered.

**Inbound connections:** binds to `0.0.0.0:KD_PORT`.

**Outbound connections:**
- PostgreSQL on port 5432 via libpqxx. Internal to the Docker network; not reachable externally.
- Blockchain sidecar on `KD_BLOCKCHAIN_SIDECAR_URL`. Connection timeout 30 seconds, read timeout 60 seconds. Sidecar failure is non-fatal: message creation completes and the blockchain digest field is left unpopulated if the sidecar is unreachable.

**Blockchain resolver thread:**
A background thread (`blockchainResolverThread_`) wakes every 60 seconds and polls `GET /pending/:id` on the sidecar for each message whose `blockchain_digest` begins with the prefix `pending:`. When a batch is confirmed on-chain, the thread updates the `blockchain_digest` column in the database with the transaction hash. Shutdown is cooperative: the thread checks `stopBlockchainResolver_` every second.

### 2.3 PostgreSQL 16

Relational database storing all server-side persistent state. Runs as a Docker container (`postgres:16`). The schema is created at startup by `Database::initSchema_()` using `CREATE TABLE IF NOT EXISTS`, making it idempotent.

**Schema:**

| Table | Key columns | Notes |
|-------|------------|-------|
| `users` | `id`, `username`, `password_hash`, `public_key` | `password_hash` is an Argon2id encoded string; `public_key` is the JSON X3DH key bundle. One-time prekeys are stored inline in the bundle and consumed transactionally on message receipt. |
| `conversations` | `id`, `name`, `created_at` | `created_at` is Unix milliseconds. |
| `conversation_participants` | `conversation_id`, `user_id` | Composite primary key. |
| `messages` | `id`, `conversation_id`, `sender_id`, `payload`, `timestamp`, `blockchain_digest` | `payload` is ciphertext only; plaintext is never stored. `blockchain_digest` holds either an empty string (sidecar unavailable), a `pending:<uuid>` value while the batch is unconfirmed, or a `0x...` transaction hash after on-chain confirmation. |
| `message_access` | `message_id`, `user_id`, `granted_by`, `revoked_at` | Per-message access control. Rows are inserted at message creation for each conversation participant. Access is revoked by setting `revoked_at` to a Unix millisecond timestamp. Revoked rows are excluded from message retrieval queries. |

All queries use parameterised statements via the pqxx prepared query API, preventing SQL injection.

**Access:** the PostgreSQL port is only exposed on the internal Docker network. In the Docker Compose configuration a host port mapping exists for `POSTGRES_PORT` (development use), but the database is not publicly routable.

### 2.4 Blockchain Sidecar

A Node.js/Express microservice (`blockchain/sidecar/index.js`) that bridges the C++ server to the Ethereum network. It runs alongside `kds` on the same host and is not included in the Docker Compose file; it is started separately.

**Batching design:**
Rather than submitting one Ethereum transaction per message, the sidecar accumulates messages in an in-memory queue and flushes a batch to the smart contract every 5 minutes (configurable via `BATCH_INTERVAL_MS`). This reduces gas costs. At flush time, the sidecar:
1. Computes `keccak256(ciphertext)` for each queued message to produce a leaf hash.
2. Builds a Merkle tree from the leaf hashes (pairs sorted before hashing; odd-length levels duplicate the last leaf).
3. Calls `recordBatch(root, leaves[])` on the smart contract with the Merkle root and all individual leaf hashes.
4. Waits for transaction confirmation, then records the transaction hash and batch ID against each `pendingId` in an in-memory results map.

**HTTP API (internal, called by kds):**

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/record` | Queue a message for the next batch. Body: `{ conversationId, msgId, ciphertext }`. Returns `{ pendingId }`. |
| `GET` | `/pending/:id` | Poll status of a queued message. Returns `{ status: "pending" \| "confirmed" \| "failed", txHash?, batchId? }`. |
| `GET` | `/health` | Health check. Returns `{ status: "ok", queueLength }`. |

**Configuration** (`.env` file):

| Variable | Description |
|----------|-------------|
| `SEPOLIA_RPC_URL` | Sepolia JSON-RPC endpoint URL |
| `PRIVATE_KEY` | Ethereum wallet private key for signing transactions |
| `CONTRACT_ADDRESS` | Deployed `MessageIntegrity` contract address |
| `PORT` | Listening port (default `3001`) |
| `BATCH_INTERVAL_MS` | Batch flush interval in milliseconds (default `300000`, i.e. 5 minutes) |

**Outbound:** HTTPS JSON-RPC calls to `SEPOLIA_RPC_URL`. Uses ethers.js v6 `JsonRpcProvider`.

### 2.5 MessageIntegrity Smart Contract

A Solidity 0.8.20 contract (`blockchain/contracts/MessageIntegrity.sol`) deployed on the Ethereum Sepolia testnet at:

```
0x8955A2361A978A41F721e58B2766b614768Db596
```

The contract stores a `mapping(uint256 => Batch)` where each `Batch` holds a Merkle root (`bytes32`), a block timestamp, and a message count. Write access is restricted to the deploying address via an `onlyOwner` modifier. The `recordBatch` function emits a `BatchRecorded` event that includes all individual leaf hashes, enabling independent verification without a server.

```solidity
event BatchRecorded(
    uint256 indexed batchId,
    bytes32 root,
    uint256 timestamp,
    uint256 messageCount,
    bytes32[] leaves
);
```

The contract does not store message content or plaintext. It provides only tamper-evidence: the on-chain Merkle root cannot be altered after submission due to the immutability of Ethereum state.

### 2.6 Verification Page

A standalone HTML/JavaScript page (`blockchain/verification/index.html`) requiring no server-side component. It is served as a static file.

Verification procedure:
1. The user pastes the message ciphertext and the Ethereum transaction hash.
2. The page computes `keccak256(ciphertext)` using ethers.js v6 in the browser (`ethers.keccak256(ethers.toUtf8Bytes(ciphertext))`).
3. It fetches the transaction receipt from the Sepolia network, trying two public RPC endpoints in sequence (`ethereum-sepolia-rpc.publicnode.com`, `rpc.sepolia.org`) with a fallback on failure.
4. It parses the `BatchRecorded` event log from the receipt to retrieve the on-chain Merkle root, all emitted leaf hashes, the batch timestamp, and the batch ID.
5. It checks that the computed leaf hash appears in the emitted leaves array.
6. It independently rebuilds the Merkle root from the emitted leaves using the same sorted-pair algorithm as the sidecar and confirms it matches the on-chain root.
7. It displays a clear VERIFIED or TAMPERED result, including the recorded UTC timestamp, block number, batch ID, message count, and the specific leaf and root hashes involved.

The page operates entirely client-side; no data is sent to any Kingdom server.

---

## 3. API Endpoint Summary

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| `GET` | `/health` | None | Server health check |
| `GET` | `/` | None | API name and version |
| `POST` | `/signup` | None | Register user; accepts `username`, `password`, `publicKey` (X3DH bundle). Returns JWT. |
| `POST` | `/login` | None | Authenticate with `username` and `password`. Returns JWT. |
| `POST` | `/logout` | Bearer JWT | Revoke session token; inserts token into in-memory blacklist. |
| `GET` | `/users` | Bearer JWT | List all registered users (id + username). |
| `GET` | `/users/{id}/public-key` | None | Fetch a user's X3DH public key bundle. Public endpoint to allow key exchange without prior authentication. |
| `POST` | `/conversations` | Bearer JWT | Create a new conversation with a name and list of participant IDs. |
| `GET` | `/users/{id}/conversations` | Bearer JWT | List conversations the authenticated user participates in. Enforces that `id` matches the authenticated user's ID. |
| `POST` | `/conversations/{id}/messages` | Bearer JWT | Send an encrypted message. Consumes the referenced one-time prekey transactionally. Queues ciphertext with the blockchain sidecar. |
| `GET` | `/conversations/{id}/messages` | Bearer JWT | Retrieve messages the authenticated user has access to (excludes revoked messages). |
| `DELETE` | `/conversations/{id}/messages/{mid}` | Bearer JWT | Delete a message. Only the original sender may delete. |
| `DELETE` | `/conversations/{id}/messages/{mid}/access/{uid}` | Bearer JWT | Revoke a specific user's access to a message. Only the sender may revoke; sender cannot revoke their own access. |

All authenticated endpoints validate the JWT on every request via the `ValidateAuthenticated` security predicate, which also checks the in-memory token blacklist. All `POST` and `PUT` endpoints require `Content-Type: application/json`, enforced by the `ValidateUntampered` predicate. The `ValidateSenderAuthenticity` predicate checks that the `senderId` field in POST request bodies matches the authenticated user's ID.

---

## 4. Port and Protocol Summary

| Component | Port | Protocol | Direction | Notes |
|-----------|------|----------|-----------|-------|
| kds | 8080 | HTTPS (TLS 1.2+) | Inbound from kdctl | Configurable via `KD_PORT`. Docker publishes this port. |
| VM public port | 4000 | TCP (port-forward → 8080) | Inbound from internet | Ansible `port-forward.yml` configures the forward on the VM. Clients connect to `200.69.13.70:4000`. |
| PostgreSQL | 5432 | TCP (libpqxx) | kds → db | Internal Docker network only. Not publicly routable. |
| Blockchain sidecar | 3001 | HTTP (plaintext) | kds → sidecar | Localhost / internal only. Configurable via `KD_BLOCKCHAIN_SIDECAR_URL`. |
| Ethereum Sepolia RPC | 443 | HTTPS (JSON-RPC over TLS) | sidecar → Sepolia | Outbound only. Public Sepolia testnet endpoint. |
| Ethereum Sepolia RPC | 443 | HTTPS (JSON-RPC over TLS) | browser → Sepolia | Verification page only. Connects directly from the user's browser; no Kingdom server involved. |

---

## 5. TLS and Certificate Verification

### 5.1 Server-side TLS

`kds` uses `httplib::SSLServer`, which wraps OpenSSL. The server is constructed with a PEM certificate and private key loaded from paths given by `KD_TLS_CERT` and `KD_TLS_KEY`. The server refuses to start if either path is absent:

```cpp
svr_(certPath.c_str(), keyPath.c_str())
```

In the production VM deployment, the Ansible `deploy.yml` playbook generates a self-signed certificate via OpenSSL if none is present:

```
openssl req -x509 -newkey rsa:4096 -sha256 -days 365 -nodes \
  -keyout certs/server.key -out certs/server.crt \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

The certificate and key are bind-mounted into the container as read-only: `./certs:/app/certs:ro`.

The server sets the following security response headers on every response, via a post-routing handler:

```
Strict-Transport-Security: max-age=31536000; includeSubDomains
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
```

HSTS instructs browsers to always use HTTPS for the domain for one year, including subdomains.

### 5.2 Client-side Certificate Verification

All HTTPS connections from `kdctl` to `kds` are made with certificate verification explicitly enabled:

```cpp
cli.enable_server_certificate_verification(true);
```

This is called unconditionally in `makeClient()` inside `Client.cc`. The client will not connect to a server presenting an untrusted certificate unless a CA certificate is explicitly provided. A custom CA certificate path is accepted via the `KD_CA_CERT` environment variable, allowing use with the self-signed certificate in development. If no CA certificate is available and verification fails, the client raises a descriptive runtime error:

```
TLS certificate verification failed — no CA certificate provided.
Use --ca-cert to specify one.
```

### 5.3 Session Authentication

After a successful login or signup, `kds` issues a JWT signed with HMAC-SHA256 (`alg: HS256`) using the `KD_JWT_SECRET`. The minimum accepted secret length is 32 characters; the server exits at startup if the secret is shorter. The JWT payload is:

```json
{ "sub": "<userId>", "username": "<username>", "iat": <epoch_seconds>, "exp": <epoch_seconds + ttl> }
```

The `exp` claim is validated on every request: the server computes the current epoch time and rejects tokens whose expiry has passed. Signature verification uses `CRYPTO_memcmp` (constant-time comparison from OpenSSL libcrypto) to prevent timing oracle attacks.

The client sends the token on all authenticated requests:

```
Authorization: Bearer <token>
```

On logout, the token is inserted into an in-memory `TokenBlacklist` (`std::unordered_set` protected by a `std::mutex`). All subsequent requests bearing that token are rejected with HTTP 401 even if the token has not yet expired. The blacklist is in-memory only and does not survive server restart.

---

## 6. Rate Limiting and Security Hardening

### 6.1 Rate Limiting

The server implements per-IP rate limiting on all routes. The implementation is a sliding-window counter stored in an `std::unordered_map<std::string, RateLimitEntry>` protected by a `std::mutex`. Each entry stores a request count and the window start time.

The window is 60 seconds (`kRateLimitWindowSec`). The configurable maximum number of requests per window is set by `KD_RATE_LIMIT_MAX_REQUESTS` (default `10`). When a request count exceeds this limit, the server responds with HTTP 429 and a JSON error body before any route handler is called.

Rate limiting is checked first in the handlers for `POST /signup` and `POST /login`, making brute-force credential attacks substantially harder.

### 6.2 Input Validation

Field size limits are enforced server-side (defined in `Constants.hh`):

| Field | Constraint |
|-------|-----------|
| Username | 1–64 characters |
| Password | 12–72 characters |
| Password (signup) | Must contain at least one uppercase letter and one digit |
| Public key bundle | 1–8192 characters |
| Conversation name | 1–128 characters |
| Message payload | 1–65536 characters |

Numeric IDs extracted from URL path segments are parsed using `std::from_chars`, which rejects non-numeric input and overflow without exceptions.

### 6.3 SQL Injection Prevention

All database queries use parameterised statements via the pqxx API. No query uses string interpolation of user-supplied values.

### 6.4 Access Control

- Conversation messages are only returned to participants: `isParticipant` is checked before every message read or write.
- A user can only send as themselves: the `ValidateSenderAuthenticity` predicate cross-checks the JWT `sub` claim against the `senderId` field in the request body.
- A user can only list their own conversations: the `handleListUserConversations_` handler verifies that the URL path ID matches the authenticated user's ID.
- Message deletion is restricted to the original sender.
- Access revocation is restricted to the sender; a sender cannot revoke their own access.

### 6.5 One-Time Prekey Consumption

One-time prekeys are consumed within the same database transaction as message creation, using a `SELECT ... FOR UPDATE` lock to prevent concurrent reuse. If the referenced prekey is not found (already consumed), the message creation is rejected with HTTP 409.

---

## 7. Deployment and Infrastructure

### 7.1 Docker Compose

The server-side components run as Docker containers managed by Docker Compose (`docker-compose.yml`):

```
docker-compose.yml
├── app (kds)
│   ├── image: mikeyfennelly/kds:latest
│   ├── container_name: kds
│   ├── ports: ${KD_PORT:-8080}:${KD_PORT:-8080}
│   ├── volumes: ./certs:/app/certs:ro
│   ├── environment: KD_PORT, KD_DB_URL, KD_TLS_CERT, KD_TLS_KEY,
│   │               KD_JWT_SECRET, KD_JWT_TTL_SECONDS,
│   │               KD_RATE_LIMIT_MAX_REQUESTS, POSTGRES_*
│   └── depends_on: db (healthy — pg_isready check)
└── db (PostgreSQL 16)
    ├── image: postgres:16
    ├── container_name: kdb
    ├── ports: ${POSTGRES_PORT}:5432
    └── volumes: pgdata (persistent named volume)
```

`kds` does not start until PostgreSQL reports healthy via the `pg_isready` health check (interval 5 s, timeout 5 s, 5 retries). The blockchain sidecar is not included in the Compose file and is managed separately.

### 7.2 Ansible Deployment

The VM is managed with Ansible (`ansible/`). Two playbooks are defined:

- `configure-host.yml` — provisions the VM (user accounts, packages, Docker).
- `deploy.yml` — pulls the latest code from GitHub, generates a self-signed certificate if absent, writes the `.env` file from Ansible Vault secrets, stops and removes old containers, pulls the latest Docker images, and starts the stack.

The inventory defines the deployment VM at `200.69.13.70` on port `2209` (non-standard SSH port):

```ini
[deployment_vm]
updakingdom ansible_host=200.69.13.70 ansible_user=deployment
             ansible_ssh_private_key_file=~/.ssh/deployment_updakingdom
             ansible_port=2209
```

A separate `port-forward.yml` playbook configures a TCP port forward from the publicly accessible port `4000` to the internal `kds` port `8080`. Clients therefore connect to `200.69.13.70:4000`.

### 7.3 CI/CD Pipeline

GitHub Actions workflows automate build, test, and release:
- Build and test workflow: runs `cmake -B build -GNinja && cmake --build build` and the GoogleTest suite on every push.
- Docker release workflow: builds a multi-stage Docker image for `kds` and pushes it to Docker Hub (`mikeyfennelly/kds`) with a semantic version tag derived from git tags.

---

## 8. External Services and Dependencies

| Service | Purpose | Network Access |
|---------|---------|----------------|
| Ethereum Sepolia testnet | Immutable tamper-evident storage of message hash batches | Outbound HTTPS (JSON-RPC) from blockchain sidecar to configured `SEPOLIA_RPC_URL` |
| publicnode.com Sepolia RPC | Primary Sepolia JSON-RPC endpoint for verification page | Outbound HTTPS from user browser (verification page only) |
| rpc.sepolia.org | Fallback Sepolia JSON-RPC endpoint for verification page | Outbound HTTPS from user browser (verification page only) |
| Docker Hub | Container image registry for `mikeyfennelly/kds` | Outbound HTTPS from VM at deployment time |
| GitHub | Source code and CI/CD | Outbound SSH from VM at deployment time |

No inbound connections are accepted from any external service. The Ethereum interaction is entirely outbound: the sidecar submits transactions to the network; it does not accept inbound connections from the blockchain.

The sidecar's HTTP API (`POST /record`, `GET /pending/:id`) is only accessible from within the server host and is not exposed to external networks. There is no authentication on the sidecar API; network isolation (localhost binding or Docker network) is the sole access control.
