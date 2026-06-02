# Cryptographic Design Document: Kingdom

CS4455 Cybersecurity, Cryptography Minor, 2026

---

## 1. Executive Summary

Kingdom is a secure messaging application whose server stores and relays ciphertext, not message plaintext. The cryptographic implementation is in C++20 and uses vetted libraries rather than hand-written primitives:

- **libsodium** for X25519 key pairs, Ed25519 signatures, Argon2id, XChaCha20-Poly1305, base64 encoding, CSPRNG output, and secure memory clearing.
- **OpenSSL EVP** for HKDF-SHA256 and HMAC-SHA256 JWT signatures.
- **cpp-httplib over OpenSSL** for HTTPS transport with server certificate verification enabled on the client.

The message encryption design is a simplified X3DH-style construction (Marlinspike and Perrin, 2016). Each user holds an X25519 identity key, an Ed25519 signing key, one X25519 signed prekey, and a batch of X25519 one-time prekeys. To send a message, the sender fetches the recipient key bundle, verifies the signed prekey signature, performs three or four X25519 Diffie-Hellman operations, derives a 256-bit message key with HKDF-SHA256, and encrypts with XChaCha20-Poly1305. Associated data binds the ciphertext to the conversation, sender, recipient, identity keys, ephemeral key, and prekey IDs.

Passwords are never stored in plaintext on the server. Server-side password verification uses libsodium Argon2id encoded hashes. Client private keys are encrypted at rest in `~/.kingdom/keys/<username>.json` using Argon2id + HKDF-SHA256 + XChaCha20-Poly1305. Decrypted local message cache entries are also encrypted at rest in `~/.kingdom/messages/<username>.json` using a separate Argon2id + HKDF-SHA256 key with a distinct HKDF info string.

---

## 2. Threat Model

The design is evaluated against the four attacker classes specified in the project requirements.

### 2.1 Passive Network Attacker

A passive network attacker can observe all traffic between client and server but cannot modify it.

| Property | Holds? | Reason |
|---|---|---|
| Message plaintext confidentiality | Yes | Messages are encrypted end-to-end with XChaCha20-Poly1305 before upload. HTTPS also protects transport contents independently. |
| Password confidentiality on the wire | Yes | Login and signup requests are sent over HTTPS with certificate verification enabled on the client. |
| Message integrity in transit | Yes | TLS provides transport-layer integrity; the AEAD tag provides application-layer integrity independently. |
| Metadata privacy | No | The server necessarily sees authenticated user IDs, conversation IDs, server-assigned timestamps, payload sizes, and access-control metadata. |

### 2.2 Active Network Attacker

An active network attacker can modify, drop, replay, or inject network traffic.

| Property | Holds? | Reason |
|---|---|---|
| TLS man-in-the-middle protection | Yes | `kd::Client` enables server certificate verification via cpp-httplib and can be configured with `KD_CA_CERT` for a self-signed local CA certificate. |
| Ciphertext tamper detection | Yes | XChaCha20-Poly1305 decryption fails if the ciphertext, nonce, or associated data are modified. |
| Cross-conversation and cross-recipient replay | Yes | Associated data includes `conversationId`, `senderId`, `recipientId`, sender identity key, sender ephemeral key, recipient identity key, signed prekey ID, and one-time prekey ID. Replaying a ciphertext in a different context produces a different AD input and AEAD authentication fails. |
| Message suppression | No | A network attacker can drop traffic. The application does not implement delivery receipts or a transparency log for missing messages. |
| Same-context replay detection | Partial | Replaying the exact stored payload to the same recipient in the same conversation can decrypt. Each X3DH send uses a fresh ephemeral key and fresh random nonce, so two encryptions of the same plaintext produce distinct ciphertexts; however, a stored ciphertext could be replayed. The E2EE layer does not include a sender-side sequence number; the server-assigned message ID is a server-controlled value. |

### 2.3 Honest-But-Curious Server

An honest-but-curious server follows the protocol faithfully but logs and inspects everything it can observe.

| Property | Holds? | Reason |
|---|---|---|
| Message plaintext confidentiality | Yes | The server receives only the versioned encrypted JSON payload. It does not receive the X3DH shared secret, the HKDF-derived message key, or plaintext. |
| Stored ciphertext integrity | Yes | The server cannot alter a stored ciphertext without causing AEAD authentication failure on the client. |
| Password database resistance | Yes | Passwords are stored using libsodium `crypto_pwhash_str_alg` with Argon2id13 and interactive parameters. The stored value is an Argon2id encoded hash including algorithm ID, version, parameters, and salt; it cannot be trivially reversed. |
| Client private-key confidentiality | Yes | Private keys are generated locally and stored encrypted; only the public key bundle is uploaded to the server. |
| Metadata privacy | No | The server stores user records, public key bundles, conversation membership, per-message access rows, server-assigned timestamps, and blockchain transaction hashes. This metadata reveals communication graphs. |

### 2.4 Fully Compromised Server

A fully compromised server has full read/write access to the database and can return arbitrary API responses to clients.

| Property | Holds? | Reason |
|---|---|---|
| Existing ciphertext confidentiality | Yes (see note) | Stored ciphertexts still require the recipient's private key and correct AEAD key derivation to decrypt. Database compromise alone does not reveal plaintext. |
| Stored ciphertext tamper detection | Yes | Modified payloads fail AEAD verification when the client attempts to decrypt. |
| Password plaintext recovery | No (direct) | The attacker obtains Argon2id hashes, not plaintext passwords. Offline dictionary attacks are possible but expensive at interactive parameters. |
| Future first-contact confidentiality | **No** | The server can substitute an attacker-controlled key bundle for a user before the sender has pinned that user's identity. The first bundle returned for a user is trusted and pinned; if that bundle is malicious, all messages to that user are encrypted to the attacker. |
| Sender authentication for new contacts | **No** | Key substitution allows the server to impersonate a sender for contacts that have not yet pinned each other's identity keys. |
| Availability and delivery ordering | **No** | A compromised server can drop, reorder, hide, delete, or selectively return messages and public key bundles. |
| Revocation enforcement | **No** | Message-access revocation is enforced by the server. A compromised server can ignore revocation requests or re-grant access. |

**TOFU key pinning** limits server key-substitution after first contact. When `kd::Client::getPublicKey()` is called, the returned bundle is processed by `pinPublicKey()` in `libkd/src/Client.cc`. The pin stored in `~/.kingdom/keys/known_keys.json` is the JSON-serialised anchor `{"identityKey": "...", "signingKey": "..."}` keyed by user ID. If the server subsequently returns a different anchor for the same user ID, the client throws an error and refuses to use the key. This protects established contacts against silent key replacement but does not protect the first contact.

**Properties explicitly not held under full server compromise:** confidentiality of messages to new contacts (first-contact MITM), sender authentication for new contacts, message availability, delivery ordering, and revocation enforcement.

---

## 3. Construction Walkthrough

### 3.1 Registration and Key Publication

At signup, `LoginWindow::performAuth()` calls `LocalKeyStore::createForSignup(username, password)` before sending the API request. The key generation and local storage steps happen entirely on the client before any network request.

```
Client                                               Server
------                                               ------
Generate X25519 identity keypair
  (crypto_box_keypair)
Generate Ed25519 signing keypair
  (crypto_sign_keypair)
Generate X25519 signed prekey (id = 1)
  (crypto_box_keypair)
Sign signed-prekey:
  input = "kd-x3dh-signed-prekey-signature-v1"
          || uint64_le(preKeyId)
          || signedPreKeyPublicKeyBytes
  signature = Ed25519(signingSecretKey, input)
Generate 20 X25519 one-time prekeys
  (crypto_box_keypair x 20)

Build public bundle JSON:
  { version:1, algorithm:"KD-X3DH-BUNDLE-V1",
    identityKey, signingKey,
    signedPreKey:{id, publicKey, signature},
    oneTimePreKeys:[{id, publicKey}, ...] }

Encrypt private material:
  salt = randombytes(16)
  argonSecret = Argon2id(password, salt,
                         OPSLIMIT_MODERATE,
                         MEMLIMIT_MODERATE,
                         L=32)
  KEK = HKDF-SHA256(IKM=argonSecret,
                    salt=base64(salt),
                    info="kd-key-encryption-v1",
                    L=32)
  nonce = randombytes(24)
  ciphertext = XChaCha20-Poly1305.Encrypt(
                 key=KEK, nonce, AD=username,
                 plaintext=privateKeyJSON)
  Zero argonSecret, KEK

Write ~/.kingdom/keys/<username>.json (mode 0600)
  { version:2, publicKeyBundle, privateMaterial,
    kdf:{algorithm, salt, opsLimit, memLimit},
    usedOneTimePreKeyIds:[] }

POST /signup { username, password,
               publicKey: publicBundleJSON }
                                                     Hash password:
                                                       crypto_pwhash_str_alg(
                                                         OPSLIMIT_INTERACTIVE,
                                                         MEMLIMIT_INTERACTIVE,
                                                         ALG_ARGON2ID13)
                                                     Store user, hash, publicKey
                                                     Issue HS256 JWT
```

The signed prekey signature input is domain-separated with the ASCII prefix `"kd-x3dh-signed-prekey-signature-v1"` followed by the little-endian 64-bit prekey ID and the raw public key bytes. This prevents a signature produced in another context from being reused as a valid signed-prekey signature.

### 3.2 Public Key Bundle Validation and TOFU Pinning

When a client fetches another user's public key bundle via `Client::getPublicKey(userId)`, the bundle is passed through `pinPublicKey()`. The pin anchor is:

```json
{ "identityKey": "<X25519 identity public key, base64>",
  "signingKey":  "<Ed25519 signing public key, base64>" }
```

stored in `~/.kingdom/keys/known_keys.json` keyed by string user ID. If no entry exists for the user, the anchor is written and the bundle is returned. If an entry exists and the anchor matches, the bundle is returned. If the anchor does not match, an exception is thrown and the key is not used.

`LocalKeyStore::validateBundle()` additionally checks the bundle version and algorithm string, verifies that key fields have the expected byte sizes after base64 decoding, and verifies the Ed25519 signature over the signed prekey. This confirms that the signed prekey was authorized by whoever holds the signing private key in the pinned bundle.

For manual forwarding, `MainWindow::fingerprintForPublicKey()` computes a SHA-256 digest of the same anchor JSON using Qt's `QCryptographicHash::Sha256` and displays it as a formatted hex string so the user can verify the recipient's identity out of band before forwarding.

### 3.3 Sending a Message

`MainWindow::onSend()` performs the following sequence for each message:

```
Sender                                               Server
------                                               ------
GET /users/<recipientId>/public-key
  -> pinPublicKey() pins or verifies anchor
  -> validateBundle() checks signature

Select recipient signedPreKey
Select first available oneTimePreKey (if any)
Generate fresh X25519 ephemeral keypair:
  (crypto_box_keypair)

DH1 = X25519(senderIdentityPriv,
             recipientSignedPreKeyPub)
DH2 = X25519(ephemeralPriv,
             recipientIdentityPub)
DH3 = X25519(ephemeralPriv,
             recipientSignedPreKeyPub)
DH4 = X25519(ephemeralPriv,
             recipientOneTimePreKeyPub)  [if present]

IKM = DH1 || DH2 || DH3 [|| DH4]
messageKey = HKDF-SHA256(
               IKM,
               salt="kd-x3dh-session-salt-v1",
               info="kd-x3dh-message-key-v1",
               L=32)

AD = JSON { version:1,
            algorithm:"KD-X3DH-XCHACHA20POLY1305-V1",
            conversationId, senderId, recipientId,
            senderIdentityPublicKey,
            senderEphemeralPublicKey,
            recipientIdentityPublicKey,
            signedPreKeyId, oneTimePreKeyId }

nonce = randombytes(24)
ciphertext = XChaCha20-Poly1305.Encrypt(
               key=messageKey, nonce,
               AD=AD.dump(), plaintext)

Zero: ephemeralPriv, DH1..DH4, IKM, messageKey

payload = JSON { version:1,
                 algorithm, senderIdentityPublicKey,
                 senderEphemeralPublicKey,
                 signedPreKeyId, oneTimePreKeyId,
                 nonce, ciphertext }

POST /conversations/<id>/messages
  { senderId, recipientId, payload }
                                                     Verify JWT; check senderId matches sub
                                                     Check sender and recipient participation
                                                     Consume oneTimePreKeyId transactionally
                                                     Store ciphertext payload
                                                     Grant message-access rows
                                                     Queue payload for blockchain batch
```

The payload stored by the server contains only the fields required for decryption (public keys, prekey IDs, nonce, ciphertext). The plaintext, shared secret, and message key never leave the client process.

### 3.4 Receiving a Message

When `MainWindow::loadMessages()` processes incoming encrypted payloads, `LocalKeyStore::decryptMessage()` is called for each message:

```
Recipient
---------
Parse payload; check version and algorithm fields
Fetch or use in-memory cached sender bundle
  -> validateBundle() verifies signed prekey signature
Check payload.senderIdentityPublicKey ==
      pinnedBundle.identityKey
Locate recipient signedPreKey by signedPreKeyId
Locate recipient oneTimePreKey by oneTimePreKeyId (if present)

DH1 = X25519(recipientSignedPreKeyPriv,
             senderIdentityPub)
DH2 = X25519(recipientIdentityPriv,
             senderEphemeralPub)
DH3 = X25519(recipientSignedPreKeyPriv,
             senderEphemeralPub)
DH4 = X25519(oneTimePreKeyPriv,
             senderEphemeralPub)          [if present]

Derive same messageKey with HKDF-SHA256
Reconstruct identical AD JSON
Decrypt: XChaCha20-Poly1305.Decrypt(
           key=messageKey, nonce, AD, ciphertext)
If authentication tag fails -> throw, reject message
Zero: DH1..DH4, messageKey

If oneTimePreKeyId used:
  sodium_memzero the private key bytes
  Record ID in usedOneTimePreKeyIds in key file
  Erase entry from in-memory identity
```

Successful decryption proves that the sender held the private key corresponding to `senderIdentityPublicKey` (via DH1) and the ephemeral key (DH2, DH3), because those are the keys required to derive the correct message key. The AEAD associated data proves the ciphertext was intended for this exact conversation, sender, and recipient context.

### 3.5 Local Storage at Rest

Kingdom maintains two encrypted local stores, both protected by an Argon2id-derived key.

**Private key store** — `~/.kingdom/keys/<username>.json`:

```
argonSecret = Argon2id(password, 16-byte random salt,
                        OPSLIMIT_MODERATE, MEMLIMIT_MODERATE, L=32)
KEK = HKDF-SHA256(IKM=argonSecret,
                   salt=base64(salt),
                   info="kd-key-encryption-v1",
                   L=32)
ciphertext = XChaCha20-Poly1305(key=KEK, nonce=randombytes(24),
                                 AD=username, plaintext=privateKeyJSON)
```

**Local plaintext message cache** — `~/.kingdom/messages/<username>.json`:

```
argonSecret = Argon2id(password, 16-byte random salt,
                        OPSLIMIT_MODERATE, MEMLIMIT_MODERATE, L=32)
cacheKey = HKDF-SHA256(IKM=argonSecret,
                        salt=saltBytes,
                        info="kd-message-cache-encryption-v1",
                        L=32)
ciphertext = XChaCha20-Poly1305(key=cacheKey, nonce=randombytes(24),
                                 AD="kingdom-message-cache-v2\0" + username,
                                 plaintext=messageCacheJSON)
```

The cache exists so already-decrypted messages can be displayed without re-running the full X3DH derivation on every UI poll. Both files are written with owner-only permissions (`0600`) where the filesystem supports it. The two HKDF info strings are deliberately distinct: a key derived for private-key encryption cannot be confused with a key derived for message-cache encryption, even if the same password and salt were used as input.

---

## 4. Primitive and Parameter Justification

### 4.1 X25519 for Diffie-Hellman

- **Implementation:** libsodium `crypto_box_keypair` for X25519-compatible keypairs; `crypto_scalarmult` for scalar multiplication.
- **Parameters:** 32-byte Curve25519 scalar (private key), 32-byte Montgomery u-coordinate (public key). Approximately 128-bit classical security, equivalent to 3072-bit RSA (NIST SP 800-57 Part 1).
- **Security property relied upon:** Computational Diffie-Hellman hardness on Curve25519.
- **Justification:** X25519 is standardized in RFC 7748, Section 5. The Montgomery ladder implementation provides constant-time scalar multiplication, reducing timing side-channel risk. Compared to NIST P-256, Curve25519 has simpler implementation requirements and no concerns about deliberately weakened parameters. Kingdom requires compact 32-byte keys, fast key agreement on modest hardware, and mature library support, all of which X25519 provides. The 128-bit security level is appropriate given that the only standard alternative offering a higher level (X448) provides 224-bit security at approximately twice the computational cost, which is unnecessary for this deployment.

### 4.2 Ed25519 for Signed Prekey Authorization

- **Implementation:** libsodium `crypto_sign_keypair`, `crypto_sign_detached`, `crypto_sign_verify_detached`.
- **Parameters:** 32-byte public key, 64-byte secret key, 64-byte Schnorr-like signature, approximately 128-bit classical security.
- **Security property relied upon:** Existential unforgeability under chosen-message attack (EUF-CMA) of the signed prekey binding.
- **Justification:** Ed25519 is specified in RFC 8032, Section 5.1. It provides deterministic signatures, which avoids the catastrophic ECDSA failure mode where a weak or reused nonce reveals the private key. Kingdom uses Ed25519 solely to bind the X25519 signed prekey to the user's identity — a recipient verifying a message can confirm that the signed prekey used by the sender was authorized by the true identity key holder. Message authentication itself comes from the AEAD, not from the signature.

### 4.3 HKDF-SHA256

- **Implementation:** OpenSSL EVP KDF with algorithm `"HKDF"` and digest `"SHA256"`, via `EVP_KDF_derive`.
- **Output length:** 32 bytes in all uses.
- **Instantiations and domain separation:**

| Context | info string | salt source |
|---|---|---|
| X3DH message key | `"kd-x3dh-message-key-v1"` | ASCII string `"kd-x3dh-session-salt-v1"` |
| Private key encryption | `"kd-key-encryption-v1"` | base64 of local random salt |
| Message cache encryption | `"kd-message-cache-encryption-v1"` | raw local random salt bytes |

- **Security property relied upon:** HKDF's extract-then-expand construction: the extract step conditions potentially non-uniform input keying material into a pseudorandom key; the expand step produces output indistinguishable from random under HMAC-SHA256.
- **Justification:** RFC 5869, Section 2 defines HKDF. Section 3.1 permits a non-secret salt; the entropy for the X3DH message key comes from the X25519 DH outputs, not the salt. The fixed salt in the X3DH context binds the derivation to this protocol without consuming key material. Separate info strings implement domain separation (RFC 5869, Section 3.2): a key derived in one context cannot be confused with a key derived in another, even given the same IKM. SHA-256 provides 256 bits of HMAC output, appropriate for deriving a 256-bit AEAD key.

### 4.4 XChaCha20-Poly1305 AEAD

- **Implementation:** libsodium `crypto_aead_xchacha20poly1305_ietf_encrypt` and `_decrypt`.
- **Parameters:** 256-bit key, 192-bit nonce, 128-bit Poly1305 authentication tag.
- **Nonce strategy:** every encryption generates a fresh 24-byte nonce from `randombytes_buf`, which draws from the OS CSPRNG.
- **Security property relied upon:** IND-CPA confidentiality and INT-CTXT ciphertext integrity, provided nonces are not reused under the same key.
- **Justification:** The underlying ChaCha20 stream cipher is specified in RFC 8439. XChaCha20-Poly1305 extends the nonce from 96 to 192 bits using the HChaCha20 construction. The 192-bit nonce makes random generation safe: the birthday bound for nonce collision under random selection is approximately 2^96 messages per key, far beyond any practical message volume. Kingdom additionally derives a fresh message key for each sent payload, because each send uses a new ephemeral X25519 keypair. Nonce collision under the same key therefore requires both a nonce collision (probability approximately 2^{-96} per pair) and key reuse (which requires identical X25519 inputs, impossible with fresh random ephemeral keys). XChaCha20-Poly1305 is preferred over AES-GCM for its larger nonce space and its resistance to cache-timing attacks on systems without AES-NI hardware acceleration.

### 4.5 Associated Data

Message AEAD associated data (`LocalKeyStore::associatedData()`) binds the ciphertext to: protocol version, algorithm string, conversation ID, sender ID, recipient ID, sender identity public key, sender ephemeral public key, recipient identity public key, signed prekey ID, and optional one-time prekey ID.

This binding is critical because the server is untrusted. Without AD, a server could copy a valid ciphertext into a different conversation, attribute it to a different sender, or present it to a different recipient. With AD, any change to the binding context produces a different authentication input and decryption fails. The AD is serialised as `json.dump()` using nlohmann/json's deterministic output, which ensures both sender and recipient construct identical byte sequences.

Local store AD serves a similar purpose. The key file uses the username as AD, preventing an encrypted blob from one account being silently decrypted by another account. The message cache uses `"kingdom-message-cache-v2\0" + username` (the null byte is an additional separator), preventing cross-context confusion between the two store types.

### 4.6 Argon2id — Password Hashing and Local Key Derivation

**Server-side password verification:**

- **Implementation:** libsodium `crypto_pwhash_str_alg` (hash) and `crypto_pwhash_str_verify` (verify).
- **Algorithm and version:** Argon2id, version 1.3.
- **Parameters:** `crypto_pwhash_OPSLIMIT_INTERACTIVE` (3 iterations) and `crypto_pwhash_MEMLIMIT_INTERACTIVE` (64 MiB memory).
- **Password policy:** 12–72 characters, at least one uppercase letter, at least one digit. The upper bound of 72 characters avoids ambiguous truncation behaviour at bcrypt's 72-byte input limit (relevant to libraries that fall back to bcrypt internally), though libsodium's Argon2id does not truncate.
- **Justification:** Argon2id is the winner of the Password Hashing Competition and is recommended by RFC 9106, Section 4 for general password hashing. The id variant uses both data-dependent and data-independent memory access, providing resistance against both time-memory trade-off attacks (from the data-dependent pass) and side-channel leakage (from the data-independent pass). Interactive parameters are appropriate for server-side login: they impose approximately 500 ms latency at moderate hardware, which is acceptable for human login but costly enough to slow bulk cracking. `crypto_pwhash_str_alg` automatically generates and embeds a random 16-byte salt, so each stored hash is distinct even for identical passwords.

**Client-side local key derivation:**

- **Parameters:** `crypto_pwhash_OPSLIMIT_MODERATE` (6 iterations) and `crypto_pwhash_MEMLIMIT_MODERATE` (256 MiB memory).
- **Justification:** Local stores are attacked offline if the local filesystem is compromised. The moderate parameters (RFC 9106 Section 4: roughly 4x more expensive than interactive) are appropriate because: the derivation runs once per login session rather than per message; the client is a desktop application and can afford higher latency than a web server; and an offline attacker has no rate limiting. The `MessageStore::kSaltSize` is 16 bytes, equal to the minimum Argon2 salt size (RFC 9106, Section 3.1) and the libsodium `crypto_pwhash_SALTBYTES` constant. Separate random salts are stored in each encrypted file, so the key file and message cache derive independent keys even if the same password is used.

### 4.7 CSPRNG and Secret Memory Clearing

All keypairs, nonces, and salts are generated via libsodium functions (`crypto_box_keypair`, `crypto_sign_keypair`, `randombytes_buf`) that draw from the operating system CSPRNG (`/dev/urandom` on Linux/macOS, `BCryptGenRandom` on Windows). No custom entropy source is used.

Sensitive intermediate values are cleared with `sodium_memzero` after use. Cleared values include: Argon2id intermediate secrets, HKDF-derived keys, ephemeral X25519 private keys, DH output vectors, IKM buffers, decrypted private-material JSON, and the GUI password string (`sodium_memzero(password.data(), password.size())` in `LoginWindow::performAuth`). Long-lived private keys (identity key, signing key, signed prekey, remaining one-time prekeys) remain in process memory for the duration of the session because they are needed to decrypt and send messages.

### 4.8 JWT Session Tokens

JWTs authenticate API requests; they are not part of the E2EE message layer.

- **Implementation:** HMAC-SHA256 via OpenSSL `HMAC(EVP_sha256(), ...)`. Signature comparison uses `CRYPTO_memcmp` to prevent timing-based signature forgery.
- **Header/claims:** `alg = HS256`, `typ = JWT`; claims `sub` (user ID string), `username`, `iat`, `exp`.
- **Secret:** `KD_JWT_SECRET` environment variable; the server rejects all authenticated requests if the variable is absent.
- **Revocation:** On logout, the token is inserted into an in-memory `std::unordered_set` (`TokenBlacklist`). Revoked tokens are rejected before JWT signature verification. The blacklist is not persisted; it is lost on server restart, making token expiry the durable session bound.
- **Justification:** HS256 satisfies the requirement of cryptographically binding session tokens to a server secret without the operational complexity of asymmetric JWT signing. HMAC-SHA256 provides 128-bit security when the secret has at least 256 bits of entropy. The constant-time comparison prevents a timing oracle that could allow signature forgery through repeated queries.

---

## 5. Protocol Citations

| Topic | Reference | Kingdom usage and deviations |
|---|---|---|
| X3DH | Marlinspike, M. and Perrin, T., "The X3DH Key Agreement Protocol", Signal, 2016, Sections 2–4 | Prekey bundle model and 3-DH/4-DH authenticated key agreement. Deviations: no Double Ratchet for post-send forward secrecy; signed prekey is not rotated automatically; one-time prekey replenishment is not implemented. |
| X25519 | RFC 7748, Section 5 | DH primitive for identity keys, signed prekeys, one-time prekeys, and ephemeral sender keys. |
| Ed25519 | RFC 8032, Section 5.1 | Signature over the signed prekey. |
| HKDF | RFC 5869, Sections 2 and 3 | Deriving AEAD keys from DH outputs and Argon2id outputs. Sections 3.1 (salt) and 3.2 (domain separation via info strings) directly inform the design. |
| ChaCha20-Poly1305 | RFC 8439 | Basis for XChaCha20-Poly1305; key size and tag size follow the RFC. |
| XChaCha20-Poly1305 | libsodium documentation, `crypto_aead_xchacha20poly1305_ietf` | Used for message payload, private key file, and local message cache. Extended nonce from 96 to 192 bits. |
| Argon2id | RFC 9106, Sections 3 and 4 | Server password hashing (interactive parameters) and local key derivation (moderate parameters). Section 4 provides the parameter-selection guidance followed here. |
| JWT | RFC 7519 | Session-token claim structure and expiry semantics. |
| HMAC | RFC 2104 | HS256 JWT signature construction. |

---

## 6. Known Limitations

1. **TOFU cannot authenticate first contact.** The first key bundle returned by the server for a user is trusted and pinned locally. If the server is compromised before first contact, it can substitute an attacker-controlled bundle, silently intercepting all messages to that user. There is no certificate authority, web-of-trust, or key transparency log to validate first-contact keys independently.

2. **No Double Ratchet.** Each message send performs an X3DH-style key establishment using a fresh sender ephemeral key, providing per-send forward secrecy for the X3DH shared secret. However, Kingdom does not implement the Signal Double Ratchet. There is no per-conversation ratchet state, skipped-message key management, or break-in recovery (post-compromise security). Compromise of a long-term private key does not forward-heal.

3. **Signed prekey rotation is not automated.** Each local identity has a single signed prekey with ID 1, generated at signup. The X3DH specification recommends periodic signed-prekey rotation (Marlinspike and Perrin, Section 2). If the signed prekey private key is compromised, all sessions relying on that prekey are weakened.

4. **One-time prekey replenishment is not implemented.** Signup generates 20 one-time prekeys. When the batch is exhausted, new messages fall back to the three-DH variant (without DH4). The three-DH variant provides weaker deniability properties and no one-time-prekey uniqueness guarantee per session.

5. **Same-context replay is not fully prevented at the E2EE layer.** Associated data prevents replay into a different conversation or recipient context, but the E2EE payload has no sender-side monotonic counter. A message stored by the server could be replayed to the same recipient in the same conversation. The server assigns unique message IDs, but this is a server-controlled value under a compromised server.

6. **Group messaging sends one copy per recipient.** The client encrypts one payload per recipient ID. For a group conversation with N participants, a production design would either send N separate encrypted payloads or use a group messaging protocol such as MLS (RFC 9420). The current architecture is strongest for one-to-one conversations; multi-party conversations have weaker sender-authentication properties.

7. **Local plaintext cache exists by design.** Successfully decrypted messages are cached in the encrypted local store for UI performance. Plaintext is held in process memory while the user is logged in and exists on disk (encrypted at rest) across sessions. An attacker with access to process memory during a session can read cached plaintexts.

8. **JWT revocation is not durable.** The logout token blacklist is an in-memory set that is lost on server restart. Session tokens remain valid (from a signature verification standpoint) until their `exp` claim expires, even if the user has logged out. Token TTL (`KD_JWT_TTL_SECONDS`, default 3600 seconds) is the durable bound.

9. **Message-layer metadata is observable by the server.** Payload sizes, sender IDs, recipient IDs, conversation membership, timestamps, and blockchain transaction hashes are all visible to the server. Traffic analysis on these metadata can reveal communication patterns even without plaintext access.

10. **Availability is outside the cryptographic guarantee.** Cryptography detects ciphertext tampering but cannot force delivery. A malicious server can refuse login, hide messages, delete database rows, return stale public key bundles, or decline to record blockchain hashes.

---

## 7. Demo Evidence for the Cryptography Requirements

- The database `messages.payload` column contains only JSON encrypted payloads with nonce and ciphertext fields; no plaintext is present.
- Modifying `payload.ciphertext`, `payload.nonce`, `conversationId`, `senderId`, `recipientId`, or any identity key field causes `crypto_aead_xchacha20poly1305_ietf_decrypt` to return a non-zero error code and the client rejects the message.
- The local key file `~/.kingdom/keys/<username>.json` stores encrypted private material under `privateMaterial.ciphertext`; private keys are not present in the file in plaintext.
- The local message cache `~/.kingdom/messages/<username>.json` version 2 stores a ciphertext envelope, not raw decrypted message text.
- Changing a user's pinned `identityKey` or `signingKey` entry in `known_keys.json` or presenting a different bundle from the server causes `pinPublicKey()` to throw, aborting the operation.
- Server password rows contain Argon2id encoded hash strings (with embedded algorithm ID, version, parameters, and salt), not user passwords.
