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
- `MessageStore` public-key cache with `std::map<uint64_t, std::string>`, `Message::formatted()` display helper, `findBySender` using `std::find_if`, `std::sort` for STL algorithm usage
- Identity fingerprint confirmation dialog (`confirmRecipientIdentity`, `fingerprintForPublicKey`) and forward-target selection (`chooseForwardTarget`)
- Unit tests for `LocalKeyStore`, `MessageStore`, and `Conversation`

**Server (kds)**
- TLS: `httplib::SSLServer` with TLS 1.3 and CSPRNG session tokens; configurable certificate and key paths enforced on all endpoints
- IP-based rate limiting: sliding window counter per IP stored in `std::unordered_map<std::string, RateLimitEntry>` protected by `std::mutex`; configurable threshold
- PostgreSQL 16 database layer: parameterised queries via libpqxx, thread-safe access with `std::scoped_lock`, `blockchain_digest` column management (`updateMessageBlockchainDigest`, `getPendingBlockchainMessages`); initial user signup storage and `libpqxx` dependency
- JWT revocation on logout: server-side token invalidation and `GET /users` endpoint
- Security predicates: `ValidateAuthenticated` (reject requests with missing/invalid bearer tokens), `ValidateSenderAuthenticity` (verify `senderId` matches JWT claim), `ValidateUntampered` (reject requests with missing or invalid `Content-Type`)
- `feat(libkd)`: `User` domain class with encapsulated auth logic; removal of dead `UserRow` struct
- Input validation on all route handlers; auth and access-control enforcement on message, conversation, and user routes; participant access control (reject users not in a conversation); duplicate participant rejection via `std::set`
- Web security response headers: HTTPS enforcement, Content-Security-Policy, X-Frame-Options

**Blockchain**
- `MessageIntegrity.sol` smart contract, Node.js/Express sidecar, and standalone verification page (`blockchain/verification/index.html`)
- Background blockchain resolver thread (`startBlockchainResolver_`) in `kds`: polls the database for `pending:<id>` digests, queries sidecar `/pending/:id`, writes confirmed tx hash on batch finalisation
- Merkle batch design with auto-incrementing `batchId`; Hardhat deployment and test scripts; blockchain hash storage fix (key by `(conversationId, msgId)` to prevent overwrites); ABI update for `recordBatch` interface
- Sidecar service in docker-compose; Ansible deployment vars for the sidecar; non-blocking sidecar with fallback RPC

---

#### Fionn Ó Murchú — approx. 33%

**Cryptography / E2EE**
- X3DH key exchange (`LocalKeyStore.cc`): key bundle generation at registration (X25519 identity keypair, Ed25519 signing keypair, signed prekey with Ed25519 signature, 20 one-time prekeys), four-way DH computation on the sender side, HKDF-SHA256 key derivation, XChaCha20-Poly1305 AEAD encryption and decryption; associated data binds `conversationId`, `senderId`, `recipientId`, and the public keys used
- HKDF derivation for local key encryption at rest; private key decryption at login
- One-time prekey consumption access control: server-side enforcement preventing a prekey from being consumed more than once
- Password hashing with libsodium Argon2id (`OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE`) at signup; password strength enforcement (minimum 12 characters, at least one uppercase letter, at least one digit)
- Local private key encryption at rest: identity key file encrypted under a key derived from the user's password via Argon2id + HKDF-SHA256; decrypted only in memory at login
- Encrypted local message store (v2): `MessageStore` upgraded to an envelope format using Argon2id + HKDF-SHA256 + XChaCha20-Poly1305; automatic migration of legacy plaintext v1 stores
- TOFU public key pinning at the client: first-seen public key pinned per username; subsequent logins verify against the stored fingerprint
- X3DH message setup flow; decrypted plaintext caching in `MessageStore`; MAC binding of messages to `(conversationId, senderId, recipientId)`

**Client (kdctl)**
- Initial interactive `kdctl` shell with login sessions
- JWT implementation: initial working HMAC-SHA256 token minting and verification
- Server-side message deletion and access revocation endpoints
- Message forwarding in the GUI client
- Conversation polling refactor and cleanup
- Controller refactor; cleanup of insecure kdctl subcommands

---

#### Mikey Fennelly — approx. 33%

**C++ Architecture & OOP Design**
- CMake multimodule project structure: `libkd` as a shared library (`.so`/`.dylib`) linked by both `kdctl` and `kds`, with a clean `include/kd/` public header boundary — the foundational C++ component design underpinning the entire project
- Core domain classes: `User`, `Message`, `Conversation` PODs with constructors, private fields, and `nlohmann::json` `NLOHMANN_DEFINE_TYPE_INTRUSIVE` serialisation macros; `libkd` skeleton with `Conversation.cc` and `Message.cc` translation units
- Security filter chain: `SecurityPredicate` abstract base class with pure virtual `Execute(const httplib::Request&)`, `SecurityFilterChain` owning a `std::vector<std::unique_ptr<SecurityPredicate>>`, `SecurityPredicateFactory` mapping predicate names to instances — a textbook chain of responsibility pattern demonstrating OOP inheritance, polymorphism, factory, and RAII via smart pointers
- Unit tests for the security filter chain using GoogleTest; integrated via CMake `enable_testing()` and `add_test()`
- `Controller.cc` initial implementation: `httplib` pre-routing handler applying the security filter chain to every request, `GET /health` endpoint, 404 error handler, `GET /users/:id/conversations` route; `configure.cc` loading all server parameters (host, port, DB connection string, TLS cert paths, JWT secret and TTL, rate limit threshold, sidecar URL) from environment variables
- `kdctl` subcommand scaffolding: `login`/`logout` endpoint stubs, `Client::getConversations`, configurable host/port/protocol with environment variable fallbacks; `kds` port and log-level via environment variables
- Clang-tidy and clang-format configuration; `compile_commands.json` export; build analysis scripts

**CI/CD & Infrastructure**
- GitHub Actions workflows: build, test, docker-release with semver tagging on merge to `main`; commit linting migration to commitlint; githooks; scoped workflow triggers
- Multi-stage Dockerfile for `kds`; Nix closure pre-flight check; Docker image weight reduction via closure size analysis and SBOM script
- Ansible playbooks: host configuration, port forwarding, app deployment to project virtual host (THEBURKENATOR.COM); deployment SANs for TLS cert
- Nix-based reproducible build environment: migration from devbox to a strict Nix shell, `check-nix-linkage.sh` asserting all linked libraries resolve to `/nix/store`; Nix flake devShell; pinned `nixos-25.11` branch with version assertions

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
