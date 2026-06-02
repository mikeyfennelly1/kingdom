# CS4455 Cybersecurity — Epic Project 2026
## Cover Document

---

### Group Name

**Kingdom**

### GitHub Repository

[https://github.com/mikeyfennelly1/kingdom](https://github.com/mikeyfennelly1/kingdom)

---

### Group Members

| Full Name | Student ID |
|-----------|------------|
| Patrick O'Shea | 24430668   |
| Fionn Ó Murchú | 24405744   |
| Mikey Fennelly | 24437522   |

---

### Contribution Breakdown

#### Patrick O'Shea — approx. 33%

**Client (kdctl)**
- **Qt6 GUI client**: full implementation of the desktop application comprising `LoginWindow` (signup/login form with server URL field, inline error display) and `MainWindow` (conversation list, message list with decrypted plaintext display, compose field, send/forward/delete/revoke action buttons, per-conversation `QTimer`-driven polling)
- `Client.cc` HTTP/HTTPS client wrapper: bearer token management, TLS certificate verification against a configurable CA certificate, TOFU key pinning via `known_keys.json`, key bundle fetch and fingerprint validation
- `MessageStore` public-key cache with `std::map<uint64_t, std::string>`, `Message::formatted()` display helper, `findBySender` using `std::find_if`
- Identity fingerprint confirmation dialog (`confirmRecipientIdentity`, `fingerprintForPublicKey`) and forward-target selection (`chooseForwardTarget`)
- Unit tests for `LocalKeyStore`, `MessageStore`, and `Conversation`

**Blockchain**
- Background blockchain resolver thread (`startBlockchainResolver_`) running in `kds`: polls the database for messages whose `blockchain_digest` column contains a `pending:<id>` token, queries the sidecar `/pending/:id` endpoint, and writes the confirmed transaction hash back to the database once the on-chain batch is finalised
- Wiring of sidecar HTTP calls into message creation in `Controller::handleCreateMessage_`: posts ciphertext to the sidecar `/record` endpoint and stores the returned `pendingId` as the initial digest value
- Smart contract storage mapping fix: switched from per-conversation overwriting to a Merkle batch design with an auto-incrementing `batchId`; added Hardhat deployment and test scripts
- Standalone blockchain verification web page (`blockchain/verification/index.html`)

---

#### Fionn Ó Murchú — approx. 33%

**Cryptography / E2EE**
- X3DH key exchange implementation (`LocalKeyStore.cc`): key bundle generation at registration (X25519 identity keypair, Ed25519 signing keypair, signed prekey with Ed25519 signature, 20 one-time prekeys), four-way DH computation on the sender side, HKDF-SHA256 key derivation, XChaCha20-Poly1305 AEAD encryption and decryption; associated data binds `conversationId`, `senderId`, `recipientId`, and the public keys used
- One-time prekey tracking and access control: server-side enforcement preventing a prekey from being consumed more than once; `oneTimePreKeyIdFromPayload` helper for server-side prekey removal
- Password hashing with libsodium Argon2id (`OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE`) at signup; server-side enforcement of password strength rules (minimum 12 characters, at least one uppercase letter, at least one digit)
- Local private key encryption at rest: identity key file encrypted under a key derived from the user's password via Argon2id + HKDF-SHA256; decrypted only in memory at login
- Encrypted local message store (v2): `MessageStore` upgraded to an envelope format using Argon2id + HKDF-SHA256 + XChaCha20-Poly1305; automatic migration of legacy plaintext v1 stores to the encrypted v2 format on first access
- TOFU public key pinning at the client: first-seen public key pinned per username; subsequent logins verify against the stored fingerprint
- X3DH message setup flow and decrypted plaintext caching in `MessageStore`
- Server-side message deletion and access revocation endpoints; message forwarding implementation in the GUI client; conversation polling via `QTimer`

---

#### Mikey Fennelly — approx. 33%

**Server (kds)**
- Project scaffolding: initial CMakeLists structure, `libkd` shared library skeleton, core data structures (`User`, `Message`, `Conversation` PODs with `nlohmann::json` serialisation macros), initial REST endpoint stubs
- `Controller.cc` server implementation and `configure.cc` environment-variable loading (host, port, database connection string, TLS certificate paths, JWT secret, JWT TTL, rate limit threshold, sidecar URL)
- Full REST API: auth routes (`POST /signup`, `POST /login`, `POST /logout`), user and public key routes, conversation routes (`POST /conversations`, `GET /users/:id/conversations`), message routes (`POST /conversations/:id/messages`, `GET /conversations/:id/messages`, `DELETE /conversations/:cid/messages/:mid`, `POST /conversations/:cid/messages/:mid/revoke`)
- PostgreSQL 16 database layer (`Database.cc`): schema initialisation, parameterised queries via libpqxx, thread-safe access with `std::scoped_lock`, `blockchain_digest` column management (`updateMessageBlockchainDigest`, `getPendingBlockchainMessages`)
- JWT authentication: token minting (`createSession_`) and verification (`authenticatedUserId_`) using HMAC-SHA256; configurable TTL
- IP-based rate limiting: sliding window counter per IP address stored in `std::unordered_map<std::string, RateLimitEntry>` protected by `std::mutex`; configurable maximum request count
- Security filter chain architecture (chain of responsibility pattern) with `SecurityPredicateFactory`; unit tests for the filter chain
- TLS via `httplib::SSLServer` with configurable certificate and key paths; enforced on all endpoints
- CI/CD pipeline: GitHub Actions workflows (build, test, docker-release with semver tagging), multi-stage Dockerfile for `kds`, Ansible playbooks for VM configuration and deployment to the project virtual host
- Nix-based reproducible build environment: migration from devbox to a strict Nix shell, `check-nix-linkage.sh` asserting all linked libraries resolve to `/nix/store`
- Clang-tidy and clang-format configuration; build hardening flags
- Initial `MessageIntegrity.sol` smart contract skeleton and ethers.js sidecar scaffolding

---

### Design Summaries

#### System Architecture

The system consists of three primary components:

**kdctl** is the C++20 Qt6 GUI desktop client. It presents a `LoginWindow` for signup and login (with server URL, username, and password fields) and a `MainWindow` that provides full messaging functionality: conversation list, per-conversation message history with decrypted plaintext, compose and send, message forwarding with recipient identity confirmation, access revocation, message deletion, and automatic polling via `QTimer`. The client communicates with `kds` exclusively over HTTPS/TLS using `cpp-httplib`. The `Client` class manages bearer token state and verifies TLS certificates. TOFU key pinning is enforced: the first time a public key is seen for a user it is written to `known_keys.json`, and subsequent sessions verify against the stored fingerprint, alerting the user to any change. End-to-end encryption is handled by `LocalKeyStore` and the encrypted `MessageStore`, both of which are part of `libkd`.

**kds** is the C++20 HTTPS server built on `cpp-httplib`'s `SSLServer`. It exposes a JSON REST API for authentication, conversation management, user and public key lookup, and message relay. The server stores and serves only ciphertext — plaintext is never present on the server side and is not visible even to an honest-but-curious server. The `Database` class manages a PostgreSQL 16 backend via libpqxx, with all queries parameterised to prevent SQL injection and all access serialised through a `std::mutex`. JWT tokens are issued at login and verified on every authenticated request. An IP-based rate limiter uses a sliding window to reject abusive clients with HTTP 429. A `SecurityFilterChain` applies a configurable chain of security predicates before each request reaches a handler. A background `blockchainResolverThread_` polls the database for messages with a pending blockchain digest and resolves them once the sidecar confirms on-chain inclusion.

**The blockchain sidecar** is a Node.js/Express microservice. Rather than submitting one Ethereum transaction per message — which would be prohibitively expensive in gas and introduce latency on every send — the sidecar implements a Merkle batch design. When `kds` sends a message, it posts the ciphertext to the sidecar's `POST /record` endpoint; the sidecar computes `keccak256(ciphertext)` and adds the resulting leaf hash to an in-memory queue, returning a `pendingId` immediately. Every five minutes the sidecar flushes the queue: it builds a Merkle root from all accumulated leaf hashes (pairs sorted before hashing, odd-length levels padded by duplicating the last leaf) and calls `recordBatch(root, leaves)` on the deployed smart contract in a single transaction. The `kds` background resolver thread periodically queries `GET /pending/:id` for each outstanding pending digest and, upon confirmation, writes the transaction hash to the message's `blockchain_digest` column in the database.

---

#### Cryptographic Design Summary

End-to-end encryption is based on a simplified X3DH (Extended Triple Diffie-Hellman) key agreement protocol, following the Signal specification. At registration, each client generates four key materials: an X25519 identity keypair, an Ed25519 signing keypair, a signed prekey (X25519 with an Ed25519 signature over the public key), and 20 one-time prekeys (X25519). These are uploaded to the server as a public key bundle JSON document.

To send a message, the sender fetches the recipient's key bundle and performs four X25519 Diffie-Hellman operations: sender identity key × recipient signed prekey, sender ephemeral key × recipient identity key, sender ephemeral key × recipient signed prekey, and sender ephemeral key × recipient one-time prekey. The four outputs are concatenated and passed through HKDF-SHA256 to derive a 32-byte symmetric key. The message plaintext is then encrypted under XChaCha20-Poly1305 (a 192-bit nonce AEAD scheme from libsodium). Associated data bound into the authentication tag includes the conversation ID, sender ID, recipient ID, and the public keys exchanged, preventing ciphertext relay across conversations or users. The server stores and relays only the resulting ciphertext; it cannot read plaintext or forge a valid authentication tag.

Passwords are hashed server-side using Argon2id (libsodium `OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE`) before storage. Local private key files are encrypted at rest under a 32-byte key derived from the user's password via Argon2id + HKDF-SHA256 with a random salt; the file is decrypted in memory only at login and the derived key is not persisted. Decrypted plaintext messages are cached locally in an encrypted `MessageStore` using the same derivation chain (Argon2id + HKDF-SHA256 + XChaCha20-Poly1305), with automatic migration from an earlier unencrypted v1 format.

TOFU with pinning is the trust model for public keys. The client's `known_keys.json` records the first-seen identity public key fingerprint for each username; if the server serves a different key on a subsequent session the client alerts the user before proceeding. A full cryptographic design document is provided at `docs/reports/crypto.md`.

---

#### Blockchain Design Summary

The `MessageIntegrity` Solidity smart contract is deployed on the Ethereum Sepolia testnet (address `0x8955A2361A978A41F721e58B2766b614768Db596`). The contract stores a `mapping(uint256 => Batch)` keyed on an auto-incrementing `batchId`, where each `Batch` record holds a `bytes32` Merkle root, a `uint256` block timestamp, and a `uint256` message count. The `recordBatch(bytes32 root, bytes32[] leaves)` function is restricted to the contract owner via an `onlyOwner` modifier and emits a `BatchRecorded` event containing the full leaf array, allowing independent verification without trusting any server.

The batching design is a deliberate trade-off: recording one transaction per message would result in one Sepolia transaction per send, incurring gas cost and latency on the critical path. Instead, messages accumulate in the sidecar queue and are flushed together every five minutes as a single transaction whose cost is amortised across the batch. Integrity is not weakened: because all leaf hashes are emitted in the `BatchRecorded` event and the Merkle root is stored immutably on-chain, any third party can recompute `keccak256(ciphertext)` for a given message, locate the corresponding leaf in the event log, reconstruct the Merkle root, and confirm it matches the on-chain record.

The standalone verification page (`blockchain/verification/index.html`) implements this check in the browser using ethers.js loaded from a public CDN. The user pastes the message ciphertext and the transaction hash; the page fetches the transaction receipt from Sepolia via public RPC endpoints, parses the `BatchRecorded` event to recover the root and all leaves, computes `keccak256(ciphertext)` locally, checks that the leaf is present in the emitted array, independently rebuilds the Merkle root from those leaves, and compares it against the on-chain root. A clear pass or fail result is displayed with the on-chain timestamp and block number. The page operates entirely client-side and requires no Kingdom backend.

---

### AI Tool Usage

AI coding assistants (Claude Code, Gemini) were used throughout development as implementation aids. Significant uses included generating boilerplate for CMake targets and Qt widget layouts, drafting initial structures for cryptographic code (subsequently reviewed, tested, and in several cases corrected), assisting with Ansible playbook syntax, and producing initial Solidity contract scaffolding. All AI-generated code was reviewed and tested before integration; a number of cases required manual correction or complete replacement where the generated output was incorrect or did not meet the project's security requirements. A full record of significant AI interactions is maintained in `docs/ai-log.md` and will be discussed during the interview.
