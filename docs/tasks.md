# Kingdom — Task List

Deadline: **Wednesday 3rd June 2026 at 5:00 PM**

Tasks are ordered by what to work on first. Crypto key infrastructure is mostly done — keypair generation, at-rest encryption, and public key distribution are all implemented. The remaining crypto gap is actual message encryption/decryption (tasks 8 and 9). Auth enforcement and input validation need finishing before the pentest report can be written. C++ stubs are fast wins. Docs come last.

---

- [x] **1. Deploy `MessageIntegrity.sol` to Sepolia**
  DONE. `blockchain/sidecar/.env` has `CONTRACT_ADDRESS=0xf9Ceb04B978523D92CE812386c55709002E59a53`, `SEPOLIA_RPC_URL`, and `PRIVATE_KEY` all set. Deployment artifacts exist in `blockchain/artifacts/`. Sidecar is wired into `kds` and records a tx hash on every message send.

- [x] **2. Enforce authentication on protected routes**
  DONE. Auth guard (401) added to `POST /conversations` and `GET /conversations/{id}/messages`. `GET /users/{id}/conversations` also has a 403 ownership check (see task 12).

- [x] **3. Add `PUT /users/{id}/public-key` endpoint**
  DONE — not needed as a separate endpoint. The public key is now sent in the signup request body and stored in the DB by the signup handler. `Database.cc` was updated alongside this.

- [x] **4. Add `GET /users/{id}/public-key` endpoint**
  DONE. `publicKeyController_()` in `Controller.cc` handles `GET /users/{id}/public-key`. Returns `{ userId, publicKey }` or 404. `Database::getUserPublicKey(userId)` implemented in `Database.cc`.

- [x] **5. Add `getPublicKey` / `setPublicKey` to `Client`**
  DONE. `Client::getPublicKey(userId)` is implemented in `Client.cc` and declared in `Client.hpp`. `setPublicKey` is not needed as a separate method — the public key is included in the signup request body instead.

- [x] **6. Generate X25519 keypair at signup**
  DONE. `LocalKeyStore::createForSignup(username, password)` in `LocalKeyStore.cc` generates a Curve25519 keypair via `crypto_box_keypair()`, encrypts the private key with XChaCha20-Poly1305 under an Argon2id-derived key (with random salt, stored params), and writes the result as a JSON file to `~/.kingdom/keys/{username}.json` with 600 permissions. `Client::signup()` calls this and includes the public key in the signup request body.

- [x] **7. Load private key on login**
  DONE. `LocalKeyStore::loadForLogin(username, password)` reads `~/.kingdom/keys/{username}.json`, re-derives the Argon2id key using the stored salt/params, decrypts the private key with XChaCha20-Poly1305, and returns a `LocalIdentityKey` struct. `kdctl` calls this on both login and signup via `completeLogin()` and stores the result in `ShellSession::identityKey`.

- [x] **8. Encrypt message payload before sending**
  In the `send` handler in `kdctl/src/main.cc`, before calling `client.sendMessage()`: fetch the recipient's public key via `client.getPublicKey(recipientId)` with TOFU pinning (save to `~/.kingdom/keys/{id}.pub` on first fetch, compare and warn on subsequent fetches). Generate a random 24-byte nonce. Call `crypto_box_easy(ciphertext, plaintext, nonce, recipient_pk, sender_sk)`. Set payload to `base64(nonce || ciphertext)`.

- [x] **9. Decrypt messages on receive**
  In `printMessages()` or the `messages` handler in `kdctl/src/main.cc`: for each message, base64-decode the payload to extract `nonce` (first 24 bytes) and `ciphertext`. Fetch the sender's public key (or use TOFU pinned copy). Call `crypto_box_open_easy(plaintext, ciphertext, nonce, sender_pk, own_sk)`. Display plaintext. On decryption failure, display `[decryption failed]`.

- [x] **10. Configure sidecar with deployed contract address**
  DONE. All three env vars are set in `blockchain/sidecar/.env`. `kds` calls the sidecar on every message creation and stores the returned tx hash in `blockchainDigest`.

- [x] **11. Add input validation to all route handlers**
  DONE. Length checks and empty string rejection added to signup, login, create conversation, and send message handlers in `Controller.cc`.

- [x] **12. Enforce access control — own-user-only**
  DONE. `GET /users/{id}/conversations` now returns 403 if the authenticated user doesn't match the `{id}` path parameter.

- [x] **13. Fix verification page with real contract address and ABI**
  DONE. Merge conflict resolved, `CONTRACT_ADDRESS` set to `0xf9Ceb04B978523D92CE812386c55709002E59a53`. Page fetches the `HashRecorded` event from Sepolia via a public RPC, computes keccak256 of the input ciphertext, and displays a pass/fail result with block timestamp. Works as a standalone `file://` page.

- [x] **14. Implement `Conversation::hasParticipant()`**
  DONE. Implemented in `libkd/src/Conversation.cc` using `std::find`. Declared in `libkd/include/kd/Conversation.hpp` with `[[nodiscard]]`.

- [x] **15. Implement `Message::formatted()`**
  DONE. Implemented in `libkd/src/Message.cc`, declared in `libkd/include/kd/Message.hpp` with `[[nodiscard]]`. Used in kdctl's basic `printMessages()` in place of manual formatting.

- [x] **16. Add STL algorithm usage in kdctl**
  DONE. `std::sort` on messages by timestamp before display. `std::find_if` replaces the manual participant inclusion loop in `create-conversation`.

- [ ] **17. Write cryptographic design document**
  Create `docs/crypto-design.md` (~2–6 pages, required by spec). Must cover: (1) threat model — which properties hold against passive attacker, active attacker, honest-but-curious server, fully compromised server (properties that fail in case 4 must be named explicitly); (2) construction walkthrough with diagrams for registration, key publication, send, receive, at-rest key storage; (3) justification for every primitive at parameter level with RFC/paper citations (libsodium `crypto_box` = Curve25519 + XSalsa20-Poly1305, RFC 9106 for Argon2id, RFC 5869 for HKDF); (4) TOFU trust model rationale and limitations; (5) known limitations (no forward secrecy in static `crypto_box`, server can equivocate on first key fetch).

- [ ] **18. Write penetration testing report**
  Create `docs/pentest.md` with curl commands and their actual outputs as evidence. Cover: (1) TLS enforcement — HTTPS with `--cacert` works, plain HTTP and wrong cert are rejected; (2) unauthenticated access — protected route without session token returns 401; (3) broken access control — user A's token against user B's conversation list returns 403; (4) input validation — oversized payload and SQL metacharacters return 400; (5) session token entropy — document that tokens are 32 bytes from `randombytes_buf`. Write this after tasks 2, 11, and 12 are done so all the controls are in place to test.

- [ ] **19. Write network architecture document**
  Create `docs/network-arch.md` or add a PlantUML diagram. Document all connections: `kdctl` → `kds` (HTTPS/TLS 1.3, port 8080, JWT Bearer token in Authorization header), `kds` → PostgreSQL (TCP port 5432), `kds` → blockchain sidecar (HTTP port 3001), sidecar → Sepolia via Alchemy/Infura RPC (HTTPS), VM hostname on THEBURKENATOR.COM.

- [ ] **20. Deploy server to THEBURKENATOR.COM VM**
  SSH into the team VM at `ALDERAAN.SOFTWARE-ENGINEERING.IE`. Install devbox (or manually: clang, cmake, ninja, openssl, libpqxx, libsodium, go-task, docker). Clone the repo, configure `.env`, run `docker compose up -d`, then `task build && task run`. Obtain a TLS cert for the subdomain (Let's Encrypt ideal; self-signed works if clients use `--cacert`).

- [ ] **21. Update README with E2EE and blockchain setup**
  In `README.adoc`, add: a section on E2EE key setup (where the key file lives, what happens on first signup, what happens if it's missing), and updated blockchain sidecar instructions that include how to get Sepolia credentials and the deployed contract address.

- [ ] **22. Add contract address and ABI to submission**
  Include the deployed contract address and a reference to the ABI file (`blockchain/artifacts/contracts/MessageIntegrity.sol/MessageIntegrity.json`) in the cover document or README.

- [ ] **23. Write cover document**
  Create `cover.md` at the repo root. Include: group name and project URL, full name and student ID for each member, GitHub repository URL, contribution breakdown (estimated % per person and specific features/components each person worked on), deployed smart contract address and ABI location.

- [ ] **24. Build AI prompt artefacts log**
  Populate `docs/ai-log.md` with 3–5 substantive AI interactions from the project. For each: include the prompt and response summary, a reflection on what the AI got right or wrong, and what was changed or rejected. Pick real design decisions — TLS setup, E2EE design, blockchain sidecar — not trivial autocomplete. These will be discussed in the interview.

- [ ] **25. Final submission check**
  Test a clean clone end-to-end: `devbox shell` → `docker compose up -d` → `task build` → `task run` → sign up two users, send an encrypted message, receive and decrypt it, verify the txHash in the verification page. Confirm the zipped archive includes all source, `README.adoc`, cover document, and AI artefacts log.
