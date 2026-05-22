# Kingdom — Task List

Deadline: **Wednesday 3rd June 2026 at 5:00 PM**

Tasks are ordered by what to work on first. Crypto is the biggest gap (worth 25%, entirely absent). Blockchain deployment has external dependencies (faucet, RPC key) so start that process early. Security fixes are quick but unblock the pentest report. C++ stubs are fast wins. Docs come last.

---

- [x] **1. Deploy `MessageIntegrity.sol` to Sepolia**
  DONE. `blockchain/sidecar/.env` has `CONTRACT_ADDRESS=0xf9Ceb04B978523D92CE812386c55709002E59a53`, `SEPOLIA_RPC_URL`, and `PRIVATE_KEY` all set. Deployment artifacts exist in `blockchain/artifacts/`. Sidecar is wired into `kds` and records a tx hash on every message send.

- [ ] **2. Enforce authentication on protected routes**
  `ValidateAuthenticated` in `SecurityPredicates.hh` is a stub that always passes — unauthenticated requests currently reach every route. Fix it, or add `authenticatedUserId_()` guards directly in the relevant route handlers. Routes that must require a valid session: `POST /conversations`, `POST /conversations/{id}/messages`, `GET /conversations/{id}/messages`, `GET /users/{id}/conversations`. Return 401 if no valid session. This also unblocks writing the pentest report.

- [ ] **3. Add `PUT /users/{id}/public-key` endpoint**
  In `Controller.cc`, add a route that takes `{ "publicKey": "<base64>" }` and calls a new `Database::updateUserPublicKey(userId, key)` method. Requires a valid session and the authenticated user must match `{id}`. The `public_key` column already exists in the `users` table. This is the first server-side piece needed for E2EE.

- [ ] **4. Add `GET /users/{id}/public-key` endpoint**
  In `Controller.cc`, add a route that returns `{ "publicKey": "<base64>" }` for a given user. Calls a new `Database::getUserPublicKey(userId)` method. Required so the sender can fetch the recipient's key before encrypting.

- [ ] **5. Add `getPublicKey` / `setPublicKey` to `Client`**
  Add two methods to `libkd/include/kd/Client.hpp` and implement them in `libkd/src/Client.cc` to wrap the two new endpoints. `setPublicKey` sends a PUT with the base64 public key. `getPublicKey` fetches and returns the key string. These are called by kdctl for E2EE.

- [ ] **6. Generate X25519 keypair at signup**
  In the signup handler in `kdctl/src/main.cc`: call `crypto_box_keypair(pk, sk)` from libsodium to produce a 32-byte public key and 32-byte private key. Derive a 32-byte symmetric key from the user's password using Argon2id (`crypto_pwhash`, INTERACTIVE params, a separate salt from server-side password hashing). Encrypt `sk` with `crypto_secretbox_easy` using a random nonce. Save `nonce || encrypted_sk` to `~/.kingdom/{username}.key`. Base64-encode `pk` and call `client.setPublicKey(userId, pk)`.

- [ ] **7. Load private key on login**
  In the login handler in `kdctl/src/main.cc`, after a successful login response: read `~/.kingdom/{username}.key`, derive the decryption key from the entered password using Argon2id (same params and salt used at signup), decrypt the stored private key with `crypto_secretbox_open_easy`, and hold the raw `sk` bytes in the `ShellSession` struct for the duration of the session.

- [ ] **8. Encrypt message payload before sending**
  In the `send` handler in `kdctl/src/main.cc`, before calling `client.sendMessage()`: fetch the recipient's public key via `client.getPublicKey(recipientId)` with TOFU pinning (save to `~/.kingdom/keys/{id}.pub` on first fetch, compare and warn on subsequent fetches). Generate a random 24-byte nonce. Call `crypto_box_easy(ciphertext, plaintext, nonce, recipient_pk, sender_sk)`. Set payload to `base64(nonce || ciphertext)`.

- [ ] **9. Decrypt messages on receive**
  In `printMessages()` or the `messages` handler in `kdctl/src/main.cc`: for each message, base64-decode the payload to extract `nonce` (first 24 bytes) and `ciphertext`. Fetch the sender's public key (or use TOFU pinned copy). Call `crypto_box_open_easy(plaintext, ciphertext, nonce, sender_pk, own_sk)`. Display plaintext. On decryption failure, display `[decryption failed]`.

- [x] **10. Configure sidecar with deployed contract address**
  DONE. All three env vars are set in `blockchain/sidecar/.env`. `kds` calls the sidecar on every message creation and stores the returned tx hash in `blockchainDigest`.

- [ ] **11. Add input validation to all route handlers**
  PARTIAL. JSON parse errors already return 400 and missing required fields are caught. What's missing: length checks (max 64 chars on username, max 72 on password, max 128 on conversation name, max 65536 on payload) and empty string rejection. Add these to every handler in `kds/src/controller/Controller.cc` before any DB call.

- [ ] **12. Enforce access control — own-user-only**
  PARTIAL. The message send endpoint already checks that the authenticated user matches `senderId` and returns 403 if not. Missing: the same check on `GET /users/{id}/conversations` (nothing stops user A fetching user B's conversation list). Also apply it to the new `PUT /users/{id}/public-key` endpoint once that's added.

- [x] **13. Fix verification page with real contract address and ABI**
  `blockchain/verification/index.html` has an unresolved git merge conflict — the file contains both the placeholder address and the real `0xf9Ceb04B978523D92CE812386c55709002E59a53`. Resolve the conflict markers first. Then verify the ABI matches the deployed contract. Test the full flow as a standalone file (opened via `file://`, no server needed): paste a real txHash and message plaintext, confirm keccak256 matches the on-chain `HashRecorded` event, and a clear pass/fail result with timestamp is shown.

- [ ] **14. Implement `Conversation::hasParticipant()`**
  In `libkd/src/Conversation.cc`, implement `bool Conversation::hasParticipant(uint64_t userId) const` using `std::find` over `participantIds`. Declare it in `libkd/include/kd/Conversation.hpp`. This fills the stub and adds a required STL algorithm.

- [ ] **15. Implement `Message::formatted()`**
  In `libkd/src/Message.cc`, implement `std::string Message::formatted() const` returning a string like `[1716000000000] User 3: hello`. Declare it in `libkd/include/kd/Message.hpp`. Use it in kdctl's `printMessages()` in place of the current manual formatting.

- [ ] **16. Add STL algorithm usage in kdctl**
  In `kdctl/src/main.cc`, add: `std::sort` on the message list by timestamp before display, and `std::find_if` with a lambda (e.g. to find a conversation by name in the list). These are specifically called out in the C++ rubric.

- [ ] **17. Write cryptographic design document**
  Create `docs/crypto-design.md` (~2–6 pages, required by spec). Must cover: (1) threat model — which properties hold against passive attacker, active attacker, honest-but-curious server, fully compromised server (properties that fail in case 4 must be named explicitly); (2) construction walkthrough with diagrams for registration, key publication, send, receive, at-rest key storage; (3) justification for every primitive at parameter level with RFC/paper citations (libsodium `crypto_box` = Curve25519 + XSalsa20-Poly1305, RFC 9106 for Argon2id, RFC 5869 for HKDF); (4) TOFU trust model rationale and limitations; (5) known limitations (no forward secrecy in static `crypto_box`, server can equivocate on first key fetch).

- [ ] **18. Write penetration testing report**
  Create `docs/pentest.md` with curl commands and their actual outputs as evidence. Cover: (1) TLS enforcement — HTTPS with `--cacert` works, plain HTTP and wrong cert are rejected; (2) unauthenticated access — protected route without session token returns 401; (3) broken access control — user A's token against user B's conversation list returns 403; (4) input validation — oversized payload and SQL metacharacters return 400; (5) session token entropy — document that tokens are 32 bytes from `randombytes_buf`. Write this after tasks 2, 11, and 12 are done so all the controls are in place to test.

- [ ] **19. Write network architecture document**
  Create `docs/network-arch.md` or add a PlantUML diagram. Document all connections: `kdctl` → `kds` (HTTPS/TLS 1.3, port 8080, X-KD-Session header), `kds` → PostgreSQL (TCP port 5432), `kds` → blockchain sidecar (HTTP port 3001), sidecar → Sepolia via Alchemy/Infura RPC (HTTPS), VM hostname on THEBURKENATOR.COM.

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
