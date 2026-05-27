# Network Architecture Documentation — Kingdom

CS4455 Cybersecurity | Computer Networks & Cybersecurity (Mark Burkley) | 2026

---

## 1. System Architecture Diagram

```
                         ┌─────────────────────────────────────────────────────┐
                         │  User Machine                                        │
                         │                                                      │
                         │  ┌─────────────────────────────────────────────┐    │
                         │  │  kdctl  (C++20 CLI client)                  │    │
                         │  │                                             │    │
                         │  │  ~/.kingdom/keys/<user>.json  (enc. keys)   │    │
                         │  │  ~/.kingdom/messages/<user>.json (cache)    │    │
                         │  └─────────────────┬───────────────────────────┘    │
                         │                    │  HTTPS / TLS                   │
                         └────────────────────┼───────────────────────────────-┘
                                              │  port 8080
                                              │
                         ┌────────────────────▼────────────────────────────────┐
                         │  Server (THEBURKENATOR.COM VM)                      │
                         │                                                      │
                         │  ┌──────────────────────────────────────────────┐   │
                         │  │  kds  (C++20 HTTPS server)                   │   │
                         │  │  httplib::SSLServer  · TLS cert/key from env │   │
                         │  │  Security filter chain (JWT, sender auth,    │   │
                         │  │  content-type validation)                    │   │
                         │  └──────────┬─────────────────┬─────────────────┘   │
                         │             │  libpqxx/TCP     │  HTTP               │
                         │             │  port 5432       │  port 3001          │
                         │  ┌──────────▼──────────┐  ┌───▼──────────────────┐  │
                         │  │  PostgreSQL 16       │  │  Blockchain sidecar  │  │
                         │  │  (Docker container)  │  │  (Node.js / Express) │  │
                         │  └─────────────────────┘  └───────────┬──────────┘  │
                         │                                        │  HTTPS/RPC  │
                         └────────────────────────────────────────┼─────────────┘
                                                                  │
                                                    ┌─────────────▼──────────────┐
                                                    │  Ethereum Sepolia Testnet  │
                                                    │  Contract:                 │
                                                    │  0xCBc9381...B9d63         │
                                                    └────────────────────────────┘


  Standalone (browser, no backend required):
  ┌──────────────────────────────────────────────┐
  │  Verification Page (HTML/JS)                 │
  │  - Accepts message ciphertext + tx hash      │
  │  - Fetches on-chain record via Sepolia RPC   │
  │  - Displays pass/fail fidelity result        │
  └──────────────────────────────────────────────┘
```

---

## 2. Components

### 2.1 kdctl — C++20 CLI Client

`kdctl` is the user-facing client executable, built in C++20 using cpp-httplib and libsodium. It runs on the user's local machine and provides an interactive shell for registration, login, conversation management, and end-to-end encrypted messaging.

**Local storage:**
- `~/.kingdom/keys/<username>.json` — private key bundle, encrypted at rest with XChaCha20-Poly1305 under an Argon2id-derived key encryption key. Permissions `0600`.
- `~/.kingdom/keys/known_keys.json` — TOFU public key store, maps user IDs to pinned identity key anchors.
- `~/.kingdom/messages/<username>.json` — local plaintext message cache. Permissions `0600`.

**Outbound connections:**
- One HTTPS connection to `kds` per API call (cpp-httplib creates and closes a connection per request).
- All connections use `enable_server_certificate_verification(true)`. A custom CA certificate can be provided via `--ca-cert` or `KD_CA_CERT`; if not provided, the system CA store is used.

### 2.2 kds — Kingdom Data Server

`kds` is the C++20 server executable. It exposes a REST API over HTTPS, handles authentication, stores ciphertext messages in PostgreSQL, and calls the blockchain sidecar on each message send.

**Configuration** (environment variables, required at startup):

| Variable | Description | Default |
|----------|-------------|---------|
| `KD_PORT` | Listening port | `8080` |
| `KD_DB_URL` | PostgreSQL connection string | — (required) |
| `KD_TLS_CERT` | Path to TLS certificate file | — (required) |
| `KD_TLS_KEY` | Path to TLS private key file | — (required) |
| `KD_JWT_SECRET` | HMAC-SHA256 secret for JWT signing (min 32 chars) | — (required) |
| `KD_JWT_TTL_SECONDS` | JWT lifetime in seconds | `3600` |
| `KD_BLOCKCHAIN_SIDECAR_URL` | Sidecar base URL | `http://localhost:3001` |
| `KD_LOG_LEVEL` | spdlog log level | `info` |

**Transport:** `httplib::SSLServer` loaded with the certificate and key at startup. All API traffic is HTTPS; the server does not offer a plaintext HTTP fallback.

**Inbound connections:** accepts connections on `0.0.0.0:KD_PORT`.

**Outbound connections:**
- PostgreSQL on port 5432 via libpqxx (internal, Docker network in production).
- Blockchain sidecar on `KD_BLOCKCHAIN_SIDECAR_URL` (HTTP, internal). Connection timeout 30 s, read timeout 60 s. Failure is non-fatal — message creation proceeds regardless.

### 2.3 PostgreSQL 16

Relational database storing users, conversations, participants, and messages. Runs as a Docker container (`postgres:16`).

**Schema:**
- `users(id, username, password_hash, public_key)` — `password_hash` is an Argon2id encoded hash; `public_key` is the JSON X3DH key bundle.
- `conversations(id, name, created_at)`
- `conversation_participants(conversation_id, user_id)`
- `messages(id, conversation_id, sender_id, payload, timestamp, blockchain_digest)` — `payload` is ciphertext only; plaintext is never written to the database.

**Access:** only accessible from within the Docker network. No external port exposure in production.

### 2.4 Blockchain Sidecar

A Node.js/Express microservice that bridges the C++ server to the Ethereum network. Runs on the same host as `kds`.

**Inbound:** `POST /record` from `kds` with `{ conversationId, ciphertext }`. Returns `{ txHash }` immediately after submitting the transaction, without waiting for mining confirmation (non-blocking).

**Outbound:** HTTPS JSON-RPC calls to the configured Sepolia RPC endpoint (`SEPOLIA_RPC_URL`). Uses ethers.js v6.

**Configuration** (`.env`):

| Variable | Description |
|----------|-------------|
| `SEPOLIA_RPC_URL` | Sepolia JSON-RPC endpoint URL |
| `PRIVATE_KEY` | Ethereum wallet private key for signing transactions |
| `CONTRACT_ADDRESS` | Deployed `MessageIntegrity` contract address |
| `PORT` | Listening port (default `3001`) |

### 2.5 Ethereum Sepolia Testnet

Public Ethereum test network. Hosts the deployed `MessageIntegrity` Solidity contract at:

```
0xCBc9381314d6f5797E840C9DD68063C2082B9d63
```

The contract stores `keccak256(ciphertext)` per conversation and emits `HashRecorded` events with block timestamps. Transactions are submitted by the sidecar wallet; the contract enforces `onlyOwner` to prevent unauthorised hash submissions.

### 2.6 Verification Page

A standalone HTML/JavaScript page that requires no backend. Users can:
1. Paste message ciphertext and a transaction hash.
2. The page computes `keccak256(ciphertext)` using ethers.js in the browser.
3. It fetches the on-chain record from the Sepolia contract via a public RPC endpoint (with a fallback RPC configured).
4. It compares the computed hash against the stored on-chain hash and displays a pass/fail result with the recorded timestamp.

The page communicates directly with the Sepolia RPC endpoint from the browser — no server-side component is involved.

---

## 3. API Endpoint Summary

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/health` | None | Server health check |
| GET | `/` | None | API info |
| POST | `/signup` | None | Register user, upload X3DH public key bundle |
| POST | `/login` | None | Authenticate, receive JWT |
| POST | `/logout` | Bearer JWT | Revoke session token |
| GET | `/users/{id}/public-key` | None | Fetch user's X3DH public key bundle |
| POST | `/users/{id}/one-time-prekeys/{id}/consume` | Bearer JWT | Mark one-time prekey as used |
| POST | `/conversations` | Bearer JWT | Create conversation |
| GET | `/users/{id}/conversations` | Bearer JWT | List user's conversations |
| POST | `/conversations/{id}/messages` | Bearer JWT | Send encrypted message |
| GET | `/conversations/{id}/messages` | Bearer JWT | Retrieve conversation messages |

---

## 4. Port and Protocol Summary

| Component | Port | Protocol | Direction | Notes |
|-----------|------|----------|-----------|-------|
| kds | 8080 | HTTPS (TLS) | Inbound from kdctl | Configurable via `KD_PORT` |
| PostgreSQL | 5432 | TCP (libpqxx) | kds → db | Internal Docker network only |
| Blockchain sidecar | 3001 | HTTP | kds → sidecar | Internal; configurable via `KD_BLOCKCHAIN_SIDECAR_URL` |
| Ethereum Sepolia RPC | 443 | HTTPS (JSON-RPC) | sidecar → Sepolia | External; public testnet endpoint |
| Ethereum Sepolia RPC | 443 | HTTPS (JSON-RPC) | browser → Sepolia | Verification page only; direct from client browser |

---

## 5. TLS and Certificate Verification

### 5.1 Server-side TLS

`kds` uses `httplib::SSLServer`, initialised with a PEM certificate and private key at startup:

```cpp
svr_(certPath.c_str(), keyPath.c_str())
```

The certificate and key paths are loaded from `KD_TLS_CERT` and `KD_TLS_KEY`. Both are required; the server exits at startup if either is absent.

In the local development environment a self-signed certificate is used (`certs/server.crt`). In production deployment on the team VM, the certificate is issued for the team's virtual host domain.

The server sets the following security headers on all responses:

```
Strict-Transport-Security: max-age=31536000; includeSubDomains
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
```

### 5.2 Client-side Certificate Verification

`kdctl` creates all HTTPS connections via cpp-httplib with certificate verification explicitly enabled:

```cpp
cli.enable_server_certificate_verification(true);
```

A custom CA certificate path can be provided at startup via `--ca-cert` or the `KD_CA_CERT` environment variable. If no custom certificate is provided, cpp-httplib falls back to the system CA store. In the development environment with a self-signed certificate, the CA cert path must be provided explicitly.

When using a self-signed certificate in development:
```bash
kdctl --ca-cert certs/server.crt
```

### 5.3 Session Authentication

After login or signup, the server issues a JWT signed with HMAC-SHA256 (`alg: HS256`). The JWT payload contains:

```json
{ "sub": "<userId>", "username": "<username>", "iat": <epoch>, "exp": <epoch + ttl> }
```

The client includes this token in all authenticated requests:
```
Authorization: Bearer <token>
```

JWT signature verification uses `CRYPTO_memcmp` (constant-time comparison) to prevent timing oracle attacks. Tokens are revoked server-side on logout by insertion into an in-memory blacklist checked on every authenticated request.

---

## 6. Docker Deployment

The server-side components are containerised with Docker Compose:

```
docker-compose.yml
├── app (kds)
│   ├── image: kds:latest  (built from Dockerfile)
│   ├── ports: KD_PORT:KD_PORT (default 8080:8080)
│   ├── volumes: ./certs:/app/certs:ro
│   └── depends_on: db (healthy)
└── db (PostgreSQL 16)
    ├── image: postgres:16
    ├── ports: POSTGRES_PORT:5432
    └── volumes: pgdata (persistent)
```

`kds` does not start until PostgreSQL reports healthy via the `pg_isready` health check. The blockchain sidecar is run separately (not included in the compose file) and is reached by `kds` via `KD_BLOCKCHAIN_SIDECAR_URL`.

---

## 7. External Services and Dependencies

| Service | Purpose | Network Access |
|---------|---------|----------------|
| Ethereum Sepolia testnet | Tamper-evident message hash storage | Outbound HTTPS from sidecar to public RPC endpoint |
| Public Sepolia RPC (publicnode.com + fallback) | JSON-RPC interface for Sepolia | Outbound HTTPS from sidecar; also direct from browser for verification page |
| GitHub (nixpkgs tarballs) | Pinned build dependencies | Outbound HTTPS at build time only; cached by Nix after first fetch |

No inbound connections are required from external services. The Ethereum network interaction is entirely outbound (the sidecar submits transactions; it does not expose any listener to the internet).
