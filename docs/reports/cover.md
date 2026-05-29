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
|-----------|-----------|
| Patrick O'Shea | 24430668 |
| Fionn Ó Murchú | [STUDENT_ID] |
| Mikey Fennelly | [STUDENT_ID] |

---

### Contribution Breakdown

#### Patrick O'Shea — approx. 38%

- Server-side security architecture: JWT utilities (HS256 signing, verification, expiration), security predicates (`ValidateAuthenticated`, `ValidateSenderAuthenticity`, `ValidateUntampered`), security filter chain
- Token blacklist for server-side logout revocation
- TLS/HTTPS setup: HTTPS enforcement via security headers (HSTS, `X-Frame-Options`, `X-Content-Type-Options`), TLS certificate verification enabled in client by default
- Access control: participant validation on conversation and message endpoints, senderId-matches-JWT enforcement
- Rate limiting on authentication endpoints (10 attempts per 60-second window)
- Input validation across all route handlers
- Blockchain integration: wiring sidecar HTTP calls into message creation, storing transaction hash, graceful fallback on sidecar unavailability
- On-chain storage mapping in smart contract, Hardhat tests, deployed contract ABI
- `MessageStore` class (local plaintext cache with public key map, `std::map`, `std::sort`, `std::find_if`)
- `Message::formatted()` and `messages-from` shell command
- Unit tests for `LocalKeyStore`, `MessageStore`, and `Conversation`
- PostgreSQL schema and parameterised queries in `Database.cc`
- Blockchain verification page

#### Fionn Ó Murchú — approx. 32%

- X3DH key exchange implementation (`LocalKeyStore.cc`, ~694 lines): key bundle generation, ephemeral DH operations, HKDF-SHA256 key derivation, XChaCha20-Poly1305 AEAD encryption and decryption, one-time prekey tracking
- Password hashing with libsodium Argon2id at signup
- Local private key encryption at rest (Argon2id + HKDF-SHA256 derived key encryption key)
- TOFU public key pinning on client (`known_keys.json`)
- HKDF derivation for local key encryption
- JWT implementation following lecture principles (working JWTs, claim structure)
- Client key pair generation at signup and decryption at login
- Interactive `kdctl` shell with login sessions
- `Client.cc` HTTP/HTTPS client implementation with bearer token support
- `create-conversation` and `send` bearer token forwarding in kdctl
- X3DH message setup and plaintext caching (`MessageStore` integration)
- Cleanup of insecure kdctl subcommands

#### Mikey Fennelly — approx. 30%

- Project scaffolding: initial CMakeLists structure, libkd library skeleton, data structures (`User`, `Message`, `Conversation` PODs), initial REST endpoint stubs
- Security filter chain architecture (chain of responsibility pattern, initial implementation)
- `Controller.cc` server skeleton and `configure.cc` environment-variable loading
- `kdctl` subcommands for conversations and login/logout stubs
- CI/CD pipeline: Dockerfile for kds, docker-compose integration, Ansible playbooks for VM configuration and deployment
- Nix-based reproducible build environment: migration from devbox to strict Nix shell, `check-nix-linkage.sh` script asserting all linked libraries resolve to `/nix/store` (preventing version drift between macOS and Linux builds)
- Clang-tidy and clang-format configuration, build hardening flags
- Blockchain feature: Solidity `MessageIntegrity.sol` smart contract (keccak256 hash storage, timestamps, `HashRecorded` events, owner access control), deployment to Ethereum Sepolia testnet, ethers.js sidecar service
- Unit tests for security filter chain

---

### Design Summaries

#### System Architecture

The system consists of three primary components:

- **kdctl** — C++20 command-line client. Handles user registration, login, and end-to-end encrypted message sending and receiving. Communicates with `kds` over HTTPS/TLS.
- **kds** — C++20 HTTPS server (cpp-httplib SSLServer). Implements a REST API for authentication, conversation management, and message relay. Stores ciphertext only — plaintext is never visible to the server. Backed by PostgreSQL 16.
- **Blockchain sidecar** — Node.js/Express microservice. On each message send, the server submits the message ciphertext to the sidecar, which computes `keccak256(ciphertext)` and records it on the Ethereum Sepolia testnet via a deployed Solidity smart contract.

#### Cryptographic Design Summary

End-to-end encryption is based on a simplified X3DH (Extended Triple Diffie-Hellman) key agreement protocol (Signal specification). At registration, each client generates an identity keypair (X25519), a signing keypair (Ed25519), a signed prekey (X25519 with Ed25519 signature), and 20 one-time prekeys (X25519). These are uploaded to the server as a public key bundle.

To send a message, the sender fetches the recipient's key bundle, performs four X25519 DH operations, concatenates the outputs, and derives a symmetric key via HKDF-SHA256. The message plaintext is encrypted under XChaCha20-Poly1305 (an AEAD scheme). Associated data includes the conversation ID, sender and recipient IDs, and the public keys used, binding the ciphertext to its context and preventing relay attacks.

Passwords are hashed server-side using Argon2id (libsodium `OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE`). Local private keys are encrypted at rest under a key derived from the user's password via Argon2id + HKDF-SHA256.

#### Blockchain Design Summary

A Solidity smart contract (`MessageIntegrity.sol`) is deployed on the Ethereum Sepolia testnet. It stores a `mapping(conversationId => keccak256 hash)` and a corresponding timestamp per conversation. When a message is sent, the server asynchronously calls the Node.js sidecar, which computes the hash and submits a transaction to the contract. The transaction hash is stored in the database alongside the message record. A standalone web-based verification page allows anyone to input message content, recompute its hash, retrieve the on-chain record by transaction hash, and display a pass/fail fidelity result with timestamp.

---

### AI Tool Usage

AI coding assistants (Claude Code, Gemini) were used throughout development as implementation aids. Specific uses included generating boilerplate for CMake targets, drafting initial structures for cryptographic code (subsequently reviewed and corrected), and assisting with Ansible playbook syntax. All AI-generated code was reviewed, tested, and in several cases corrected or replaced where incorrect. A full record of significant AI interactions is maintained in `docs/ai-log.md` and will be discussed during the interview.
