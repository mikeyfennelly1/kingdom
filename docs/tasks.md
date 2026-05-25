# Kingdom — Task List
Deadline: **Wednesday 3rd June 2026 at 5:00 PM**

---

## Completed

- [x] **1.** Deploy `MessageIntegrity.sol` to Sepolia. Contract at `0xf9Ceb04B978523D92CE812386c55709002E59a53`.
- [x] **2.** Enforce authentication (401) on protected routes. 403 ownership check on `GET /users/{id}/conversations`.
- [x] **3.** `PUT /users/{id}/public-key` — public key sent in signup body instead.
- [x] **4.** `GET /users/{id}/public-key` endpoint returning `{ userId, publicKey }` or 404.
- [x] **5.** `Client::getPublicKey(userId)` implemented.
- [x] **6.** X25519 keypair generated at signup. Private key encrypted with XChaCha20-Poly1305 under Argon2id-derived key. Written to `~/.kingdom/keys/{username}.json` with 0600 permissions.
- [x] **7.** `LocalKeyStore::loadForLogin()` re-derives Argon2id key, decrypts private key, stores in `ShellSession::identityKey`.
- [x] **8.** `LocalKeyStore::encryptMessage()` using `crypto_box_easy`. Returns `base64(nonce || ciphertext)`.
- [x] **9.** `LocalKeyStore::decryptMessage()` using `crypto_box_open_easy`. Shows `[decryption failed]` on failure.
- [x] **10.** Sidecar wired into `kds`. Records tx hash on every message send, stored in DB.
- [x] **11.** Input validation: username 1–64, password 1–72, conversation name 1–128, payload 1–65536.
- [x] **12.** Access control: `GET /users/{id}/conversations` returns 403 if authenticated user doesn't match `{id}`.
- [x] **13.** Verification page: computes keccak256, fetches Sepolia receipt, displays pass/fail.
- [x] **14.** `Conversation::hasParticipant()` using `std::find`. `[[nodiscard]]`.
- [x] **15.** `Message::formatted()` using `std::ostringstream`. Used in `printMessages()`.
- [x] **16.** STL algorithms in kdctl: `std::sort` on messages, `std::find_if` for participant dedup.
- [x] **17.** `ValidateAuthenticated` predicate: verifies Bearer JWT against `KD_JWT_SECRET`. `JwtUtils.hh` extracted.
- [x] **18.** Rate limiting: 10 attempts / 60s per IP on login and signup. Returns 429. `std::mutex` protected.
- [x] **19.** Sidecar error handling: `contract.recordHash()` in try/catch. Returns 500 JSON on failure.
- [x] **20.** `blockchain/MessageIntegrity.abi.json` committed.
- [x] **21.** `MessageStore` class: `add` (`std::move`), `getAll`, `findBySender` (`std::copy_if`), `clear`. `messages-from` command in kdctl.

---

## Networks & Cybersecurity — Code

- [x] **N1. Fix access control on message retrieval**
  `GET /conversations/:id/messages` does not check that the authenticated user is a participant. Any authenticated user can read any conversation. Query `conversation_participants` and return 403 if the user is not a member.

- [x] **N2. Fix access control on message send**
  `POST /conversations/:id/messages` does not verify the sender is a participant. Add the same participant check before inserting.

- [x] **N3. Fix client auth token on read calls**
  `Client::getConversations()` and `Client::getMessages()` do not send the `Authorization: Bearer` header. Server returns 401 on both calls for any authenticated session. Add the header to these requests in `Client.cc`.

- [x] **N4. Make TLS verification the default**
  When `caCertPath` is empty and the URL is HTTPS, `makeClient` falls back to a plain unverified `httplib::Client`. Certificate verification should be the default; skipping it should require an explicit flag. Fix `Client.cc` so HTTPS always enables verification (using system CA store if no custom cert is provided).

- [x] **N5. Add security response headers**
  Add `Strict-Transport-Security`, `X-Content-Type-Options: nosniff`, and `X-Frame-Options: DENY` to all server responses via a post-routing handler in `Controller.cc`.

- [x] **N6. Fix `ValidateAuthenticated` to reject missing tokens**
  `ValidateAuthenticated::Validate` currently returns `std::nullopt` (pass) when no `Authorization` header is present — it only rejects malformed or invalid tokens. A request with no auth header at all passes through the filter and reaches the route handler. Fix it to return a 401 response whenever the header is absent or does not start with `Bearer `.

- [x] **N7. Implement `ValidateSenderAuthenticity`**
  `ValidateSenderAuthenticity::Validate` is a stub that returns `std::nullopt` unconditionally. Implement it: extract the JWT-authenticated user ID from the `Authorization` header; for POST requests with a JSON body containing a `senderId` field, verify that `senderId == authenticatedUserId`. Return 403 if they differ. This prevents a user from posting messages attributed to another user even if they have a valid token.

---

## C++ — Code

- [ ] **C1. Exception handling on numeric input**
  `promptId()` in `kdctl/main.cc` calls `std::stoull` with no `try`/`catch`. Non-numeric input crashes the process. Wrap in a try/catch for `std::invalid_argument` and `std::out_of_range` and print an error instead.

- [ ] **C2. Add `std::map` or `std::set` usage**
  The spec names these containers explicitly and neither appears in the codebase. Introduce `std::map<uint64_t, std::string>` as a public-key cache in `MessageStore` (keyed by userId) to avoid redundant server lookups and demonstrate the container.

- [ ] **C3. Remove or implement `kd::User`**
  `kd::User` exists in a header with `NLOHMANN_DEFINE_TYPE_INTRUSIVE` but is never instantiated. Either use it in `Controller.cc` to deserialise user responses (replacing raw JSON field access) or delete it. Dead code is a visible weakness.

- [ ] **C4. Add meaningful unit tests**
  `SecurityTests.cc` tests only that construction doesn't throw. Add at least:
  - `LocalKeyStore` encrypt/decrypt roundtrip
  - `MessageStore::findBySender` filtering correctness
  - `Conversation::hasParticipant` true/false cases

- [ ] **C5. Implement Ed25519 message signatures**
  `Message::signature` exists in the struct and is serialised to JSON but is always `""`. Implement it: at signup, generate an Ed25519 signing keypair (`crypto_sign_keypair`) alongside the X25519 keypair. Store the Ed25519 public key on the server (`PUT /users/{id}/public-key` already exists — extend it or add a separate field). In `LocalKeyStore::encryptMessage`, sign the plaintext payload with `crypto_sign_detached` and base64-encode the 64-byte signature into `Message::signature`. In `LocalKeyStore::decryptMessage`, after decryption verify the signature with `crypto_sign_verify_detached`; throw on failure. This also makes `ValidateSenderAuthenticity` (N7) cryptographically meaningful rather than just a JWT check.

---

## Cryptography — Code

- [x] **Cr1. Add HKDF**
  The spec explicitly requires HKDF with explicit `info` strings for domain separation. Nothing in the codebase uses HKDF. Add a `LocalKeyStore::deriveKey(sharedSecret, info)` helper using `EVP_KDF` (HKDF-SHA256 from OpenSSL) and use it when deriving the key-encryption key from the Argon2id output, passing `"kd-key-encryption-v1"` as the info string. This demonstrates understanding of domain separation.

- [x] **Cr2. Implement TOFU key pinning**
  `Client::getPublicKey` fetches from the server on every call with no caching or pinning. A compromised server can silently substitute any key. On first contact with a user, store their public key in `~/.kingdom/keys/known_keys.json` (keyed by userId). On subsequent lookups compare and throw if the key has changed, warning the user explicitly.

- [x] **Cr3. Bind ciphertext to conversation context**
  `crypto_box_easy` has no associated data field. A server can copy ciphertext from one conversation into another — the recipient cannot detect this. Prepend a MAC over `conversationId || senderId || recipientId || nonce || ciphertext` using `crypto_auth` (HMAC-SHA512256) with a key derived from the DH shared secret. Verify the MAC before decrypting.

- [ ] **Cr4. Upgrade Argon2id parameters for at-rest key**
  `OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE` are tuned for fast interactive login, not for protecting a long-lived private key on disk. Change `LocalKeyStore::createForSignup()` to use `crypto_pwhash_OPSLIMIT_MODERATE` / `crypto_pwhash_MEMLIMIT_MODERATE` when deriving the key-encryption key.

- [ ] **Cr5. Acknowledge forward secrecy absence in code**
  The spec rubric explicitly requires that "forward secrecy properties (or their absence) are acknowledged honestly." The current implementation uses static long-term X25519 keys — compromise of a private key retroactively exposes all past messages. Add a `// NOTE:` comment block in `LocalKeyStore.cc` above `encryptMessage` documenting this: state that static keys are used, that there is no ephemeral key exchange or ratcheting, and what the implication is (no forward secrecy). This is a required acknowledgement, not optional.

---

## Blockchain — Code

- [ ] **B1. Add on-chain storage mapping to contract**
  `recordHash` currently only emits an event — no state is written. Add `mapping(uint256 => bytes32) public hashes` and `mapping(uint256 => uint256) public timestamps` to `MessageIntegrity.sol`. Write both in `recordHash`. Redeploy to Sepolia and update the contract address everywhere.

- [ ] **B2. Make sidecar call non-blocking**
  `await tx.wait()` in `sidecar/index.js` blocks until the transaction is mined (potentially 30–90 seconds). The C++ server waits synchronously for the HTTP response. Fire-and-forget: respond to the server with the `txHash` immediately after `contract.recordHash()` returns (before `tx.wait()`), and confirm mining in the background.

- [ ] **B3. Add Hardhat unit test for the contract**
  Add `blockchain/test/MessageIntegrity.test.js`. Deploy the contract locally, call `recordHash`, assert the `HashRecorded` event is emitted with correct args and the storage mapping is updated. Run with `npx hardhat test`.

- [ ] **B4. Remove `.env` from git and rotate key**
  `blockchain/sidecar/.env` containing `PRIVATE_KEY` is committed. Add `blockchain/sidecar/.env` and `blockchain/.env` to `.gitignore`. Generate a new testnet wallet and update the deployed contract owner if needed.

- [ ] **B5. Add fallback RPC to verification page**
  The verification page hardcodes a single public RPC (`publicnode.com`). Add a secondary fallback (e.g. `https://rpc.sepolia.org`) and retry on failure.

---

## Deployment

- [ ] **D1. Deploy server to THEBURKENATOR.COM VM**
  SSH into `ALDERAAN.SOFTWARE-ENGINEERING.IE`. Clone repo, configure `.env`, run `docker compose up -d`, `task build && task run`. Obtain TLS cert via Let's Encrypt (`certbot --nginx`) or use a self-signed cert. Update `KD_TLS_CERT` and `KD_TLS_KEY` accordingly.

---

## Docs / Submission

- [ ] **Doc1.** Cryptographic design document (`docs/crypto-design.md`)
- [ ] **Doc2.** Penetration testing report (`docs/pentest.md`)
- [ ] **Doc3.** Network architecture document (`docs/network-arch.md`)
- [ ] **Doc4.** Update README with E2EE and blockchain setup instructions
- [ ] **Doc5.** Cover document (`cover.md`)
- [ ] **Doc6.** AI prompt artefacts log (`docs/ai-log.md`) — keep up to date
- [ ] **Doc7.** Final submission check — all files committed, binaries build clean, spec checklist complete
