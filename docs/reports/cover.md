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

**Client**
- **Qt6 GUI client** (`kdctl`): full replacement of the CLI shell with a Qt6 desktop application comprising `LoginWindow` (signup/login form, error display) and `MainWindow` (conversation list, message list, compose, poll-on-timer, identity fingerprint confirmation); delete and revoke access buttons; message forwarding UI (`chooseForwardTarget`, `confirmRecipientIdentity`)
- `Client.cc` HTTP/HTTPS client: bearer token support, TLS certificate verification, TOFU key pinning (`known_keys.json`), key bundle fetch and validation
- `MessageStore` class: public-key cache with `std::map`, `std::sort`, `std::find_if`; `Message::formatted()`
- Unit tests for `LocalKeyStore`, `MessageStore`, and `Conversation`

**Blockchain**
- Wiring sidecar HTTP calls into message creation, storing transaction hash per (conversationId, msgId) pair, graceful fallback on sidecar unavailability; fixed hash keying to prevent per-conversation overwrites
- On-chain storage mapping in smart contract, Hardhat tests, deployed contract ABI
- Blockchain verification page

#### Fionn Ó Murchú — approx. 33%

**Cryptography / E2EE**
- X3DH key exchange implementation (`LocalKeyStore.cc`): key bundle generation, ephemeral DH operations, HKDF-SHA256 key derivation, XChaCha20-Poly1305 AEAD encryption and decryption, one-time prekey tracking and access control fix (preventing prekey reuse)
- Password hashing with libsodium Argon2id at signup; enforcement of password strength rules (≥12 chars, uppercase, digit) server-side
- Local private key encryption at rest (Argon2id + HKDF-SHA256 derived key encryption key)
- Encrypted local message store (v2): `MessageStore` upgraded to envelope format with Argon2id + HKDF-SHA256 + XChaCha20-Poly1305; automatic migration of legacy plaintext v1 stores to encrypted v2
- TOFU public key pinning; client key pair generation at signup and decryption at login
- X3DH message setup and plaintext caching (`MessageStore` integration)
- Server-side message deletion and access revocation endpoints; message forwarding in GUI client; conversation polling (QTimer)

#### Mikey Fennelly — approx. 33%

**Server**
- Project scaffolding: initial CMakeLists structure, libkd library skeleton, data structures (`User`, `Message`, `Conversation` PODs), initial REST endpoint stubs
- `Controller.cc` server skeleton and `configure.cc` environment-variable loading
- Security filter chain architecture (chain of responsibility pattern); unit tests for security filter chain; `SecurityPredicateFactory`
- CI/CD pipeline: GitHub Actions workflows (build, test, docker-release with semver tagging), multi-stage Dockerfile for kds with Nix closure pre-flight check, docker-compose integration, Ansible playbooks for VM configuration and deployment
- Nix-based reproducible build environment: migration from devbox to strict Nix shell, `check-nix-linkage.sh` asserting all linked libraries resolve to `/nix/store`, toolchain closure scripts
- Clang-tidy and clang-format configuration, build hardening flags
- Solidity `MessageIntegrity.sol` smart contract (keccak256 hash storage, per-message timestamps, `HashRecorded` events, `onlyOwner` access control), deployment to Ethereum Sepolia testnet, ethers.js sidecar service

---

### Design Summaries

#### System Architecture

The system consists of three primary components:

- **kdctl** — C++20 Qt6 GUI desktop client. Provides signup/login screens (`LoginWindow`) and a full messaging interface (`MainWindow`) with conversation management, encrypted message send/receive, message forwarding, access revocation, and per-conversation message polling. Communicates with `kds` over HTTPS/TLS using `cpp-httplib`.
- **kds** — C++20 HTTPS server (`cpp-httplib` SSLServer). Implements a REST API for authentication, conversation management, and message relay. Stores ciphertext only — plaintext is never visible to the server. Backed by PostgreSQL 16. Thread-safe database access via `std::scoped_lock`.
- **Blockchain sidecar** — Node.js/Express microservice. On each message send, the server submits the message ciphertext to the sidecar, which computes `keccak256(ciphertext)` and records it on the Ethereum Sepolia testnet via a deployed Solidity smart contract. The transaction hash is stored in the database alongside the message record.

#### Cryptographic Design Summary

End-to-end encryption is based on a simplified X3DH (Extended Triple Diffie-Hellman) key agreement protocol (Signal specification). At registration, each client generates an identity keypair (X25519), a signing keypair (Ed25519), a signed prekey (X25519 with Ed25519 signature), and 20 one-time prekeys (X25519). These are uploaded to the server as a public key bundle.

To send a message, the sender fetches the recipient's key bundle, performs four X25519 DH operations (identity × signed prekey, ephemeral × identity, ephemeral × signed prekey, ephemeral × one-time prekey), concatenates the outputs, and derives a symmetric key via HKDF-SHA256. The message plaintext is encrypted under XChaCha20-Poly1305 (an AEAD scheme). Associated data includes the conversation ID, sender and recipient IDs, and the public keys used, binding the ciphertext to its context and preventing relay attacks.

Passwords are hashed server-side using Argon2id (libsodium `OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE`). Local private keys are encrypted at rest under a key derived from the user's password via Argon2id + HKDF-SHA256. Decrypted plaintext messages are also stored locally in an encrypted cache (MessageStore v2: Argon2id + HKDF-SHA256 + XChaCha20-Poly1305, with automatic migration from an earlier plaintext format).

#### Blockchain Design Summary

A Solidity smart contract (`MessageIntegrity.sol`) is deployed on the Ethereum Sepolia testnet. It stores a nested `mapping(conversationId => mapping(msgId => bytes32 hash))` and a corresponding timestamp per message. When a message is sent, the server asynchronously calls the Node.js sidecar, which computes the hash and submits a transaction to the contract. The transaction hash is stored in the database alongside the message record. A standalone web-based verification page allows anyone to input message content, recompute its hash, retrieve the on-chain record by transaction hash, and display a pass/fail fidelity result with timestamp.

---

### AI Tool Usage

AI coding assistants (Claude Code, Gemini) were used throughout development as implementation aids. Specific uses included generating boilerplate for CMake targets, drafting initial structures for cryptographic code (subsequently reviewed and corrected), and assisting with Ansible playbook syntax. All AI-generated code was reviewed, tested, and in several cases corrected or replaced where incorrect. A full record of significant AI interactions is maintained in `docs/ai-log.md` and will be discussed during the interview.
