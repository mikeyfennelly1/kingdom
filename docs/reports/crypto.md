# Cryptographic Design Document: Kingdom

CS4455 Cybersecurity, Cryptography Minor, 2026

## 1. Executive Summary

Kingdom is a secure messaging application whose server stores and relays ciphertext, not message plaintext. The cryptographic implementation is in C++20 and uses vetted libraries rather than hand-written primitives:

- libsodium for X25519 key pairs, Ed25519 signatures, Argon2id, XChaCha20-Poly1305, base64 encoding, CSPRNG output, and secure memory clearing.
- OpenSSL EVP for HKDF-SHA256 and HMAC-SHA256 JWT signatures.
- cpp-httplib over OpenSSL for HTTPS transport with server certificate verification enabled on the client.

The message encryption design is a simplified X3DH-style construction. Each user has an X25519 identity key, an Ed25519 signing key, one X25519 signed prekey, and a batch of X25519 one-time prekeys. A sender fetches the recipient key bundle, verifies the signed prekey, performs three or four X25519 Diffie-Hellman operations, derives a 256-bit message key with HKDF-SHA256, and encrypts the message with XChaCha20-Poly1305. Associated data binds the ciphertext to the conversation, sender, recipient, identity keys, ephemeral key, and prekey IDs.

Passwords are never stored directly on the server. Server-side password verification uses libsodium Argon2id encoded hashes. Client private keys are encrypted at rest in `~/.kingdom/keys/<username>.json` using Argon2id + HKDF-SHA256 + XChaCha20-Poly1305. Decrypted local message cache entries are also encrypted at rest in `~/.kingdom/messages/<username>.json` using a separate Argon2id + HKDF-SHA256 key and a separate HKDF info string.

## 2. Threat Model

The design is evaluated against the four attacker classes required by the specification.

### 2.1 Passive Network Attacker

A passive network attacker can read traffic between client and server but cannot modify it.

| Property | Holds? | Reason |
| --- | --- | --- |
| Message plaintext confidentiality | Yes | Messages are encrypted end-to-end with XChaCha20-Poly1305 before upload. HTTPS also protects transport contents. |
| Password confidentiality on the wire | Yes | Login and signup requests are sent over HTTPS with certificate verification enabled. |
| Message integrity in transit | Yes | TLS protects transport integrity, and the message AEAD tag protects ciphertext integrity independently of TLS. |
| Metadata privacy | No | The server necessarily sees authenticated user IDs, conversation IDs, timestamps, payload sizes, and access-control metadata. |

### 2.2 Active Network Attacker

An active network attacker can modify, drop, replay, or inject network traffic.

| Property | Holds? | Reason |
| --- | --- | --- |
| TLS man-in-the-middle protection | Yes | `kd::Client` enables server certificate verification and can be configured with `KD_CA_CERT` for the self-signed local CA/certificate. |
| Ciphertext tamper detection | Yes | XChaCha20-Poly1305 decryption fails if ciphertext, nonce, or associated data are modified. |
| Cross-conversation or cross-recipient replay protection | Yes | Associated data includes `conversationId`, `senderId`, `recipientId`, sender identity key, sender ephemeral key, recipient identity key, signed prekey ID, and one-time prekey ID. Replaying the ciphertext in a different context causes AEAD authentication failure. |
| Message suppression protection | No | A network attacker can still drop traffic. The application does not implement delivery receipts or a transparency log for missing messages. |
| Same-context replay detection | Partial | Replaying the exact same stored payload to the same recipient in the same conversation can decrypt. The server database gives each stored message a unique ID, but the E2EE layer does not include a sender-side sequence number. |

### 2.3 Honest-But-Curious Server

An honest-but-curious server follows the protocol but logs and inspects everything it can observe.

| Property | Holds? | Reason |
| --- | --- | --- |
| Message plaintext confidentiality | Yes | The server receives only the versioned encrypted JSON payload. It does not receive the X3DH shared secret, message key, or plaintext. |
| Stored message integrity | Yes | The server cannot alter ciphertext without causing XChaCha20-Poly1305 authentication failure on the client. |
| Password database resistance | Yes | Server passwords are stored with libsodium `crypto_pwhash_str_alg` using Argon2id13 and interactive parameters. |
| Client private-key confidentiality | Yes | Private keys are generated and stored locally; only the public key bundle is uploaded. |
| Metadata privacy | No | The server stores users, public key bundles, conversations, participants, message timestamps, message access rows, and blockchain transaction hashes. |

### 2.4 Fully Compromised Server

A fully compromised server can read and modify the database, return arbitrary API responses, and selectively deliver messages.

| Property | Holds? | Reason |
| --- | --- | --- |
| Past ciphertext plaintext confidentiality | Mostly yes | Existing stored ciphertext still requires the recipient private key and AEAD key derivation to decrypt. Database compromise alone does not reveal plaintext. |
| Stored ciphertext tamper detection | Yes | Modified payloads fail AEAD verification if the client attempts to decrypt them. |
| Password plaintext recovery | Not directly | The attacker gets Argon2id password hashes, not plaintext passwords. Offline guessing is still possible. |
| Future first-contact confidentiality | No | The server can substitute a malicious key bundle for a user before the sender has pinned that user's identity/signing key. |
| Sender authentication for new contacts | No | Key substitution can make the sender encrypt to attacker-controlled keys during first contact. |
| Availability and ordering | No | A compromised server can drop, reorder, hide, delete, or selectively return messages and public keys. |
| Revocation honesty | No | The server enforces message-access revocation. A compromised server can ignore or alter access-control results. |

TOFU key pinning limits server key-substitution after first use. `kd::Client` stores a pin in `~/.kingdom/keys/known_keys.json`, using a stable anchor consisting of the recipient `identityKey` and `signingKey`. If the server later returns a different anchor for the same user ID, the client refuses to use it. This protects established contacts against silent key replacement, but it does not protect the first contact.

## 3. Construction Walkthrough

### 3.1 Registration and Key Publication

At signup, `kd::Client::signup()` calls `LocalKeyStore::createForSignup(username, password)` before sending the API request.

```text
Client                                           Server
------                                           ------
Generate X25519 identity keypair
Generate Ed25519 signing keypair
Generate X25519 signed prekey, id = 1
Sign signed-prekey public key with Ed25519
Generate 20 X25519 one-time prekeys

Build public bundle:
  version = 1
  algorithm = KD-X3DH-BUNDLE-V1
  identityKey
  signingKey
  signedPreKey { id, publicKey, signature }
  oneTimePreKeys[]

Protect local private bundle:
  salt = randombytes(16)
  secret = Argon2id(password, salt,
                    OPSLIMIT_MODERATE,
                    MEMLIMIT_MODERATE)
  KEK = HKDF-SHA256(secret,
                    salt = base64(salt),
                    info = kd-key-encryption-v1,
                    L = 32)
  nonce = randombytes(24)
  ciphertext = XChaCha20-Poly1305(
      key = KEK,
      AD = username,
      plaintext = private key JSON)

Write ~/.kingdom/keys/<username>.json with 0600 permissions

POST /signup { username, password, publicKey = public bundle }
                                                Hash password with Argon2id13
                                                Store username, password hash,
                                                public key bundle
                                                Return HS256 JWT
```

The signed prekey signature input is domain separated:

```text
"kd-x3dh-signed-prekey-signature-v1" || uint64_le(preKeyId) || signedPreKeyPublicKeyBytes
```

This prevents a signature made for some other context from being reused as a valid signed-prekey signature.

### 3.2 Public Key Bundle Validation and TOFU

When a client fetches another user's public key bundle using `Client::getPublicKey(userId)`, the returned bundle is pinned locally. The pin is not the whole bundle, because one-time prekeys legitimately change as they are consumed. Instead, the pin contains:

```json
{
  "identityKey": "<recipient X25519 identity key>",
  "signingKey": "<recipient Ed25519 signing key>"
}
```

`LocalKeyStore::validateBundle()` checks the bundle version, algorithm string, key sizes, and Ed25519 signature over the signed prekey. This confirms that the signed prekey was authorized by the holder of the signing private key contained in the pinned bundle. It does not prove the real-world identity of a first-contact user; that is the explicit TOFU limitation.

For manual forwarding, the GUI also displays a SHA-256 fingerprint of the same identity/signing-key anchor and asks the user to verify it before forwarding to a chosen recipient.

### 3.3 Sending a Message

A normal send from `MainWindow::onSend()` performs:

```text
Sender                                           Server
------                                           ------
GET /users/<recipientId>/public-key
Pin or verify recipient identity/signing key
Validate signed prekey signature

Select recipient signed prekey
Select first available recipient one-time prekey, if any
Generate fresh X25519 ephemeral keypair

DH1 = X25519(sender identity private, recipient signed prekey public)
DH2 = X25519(sender ephemeral private, recipient identity public)
DH3 = X25519(sender ephemeral private, recipient signed prekey public)
DH4 = X25519(sender ephemeral private, recipient one-time prekey public), if present

IKM = DH1 || DH2 || DH3 || optional DH4
messageKey = HKDF-SHA256(
    IKM,
    salt = "kd-x3dh-session-salt-v1",
    info = "kd-x3dh-message-key-v1",
    L = 32)

AD = JSON {
  version: 1,
  algorithm: "KD-X3DH-XCHACHA20POLY1305-V1",
  conversationId,
  senderId,
  recipientId,
  senderIdentityPublicKey,
  senderEphemeralPublicKey,
  recipientIdentityPublicKey,
  signedPreKeyId,
  oneTimePreKeyId
}

nonce = randombytes(24)
ciphertext = XChaCha20-Poly1305.encrypt(messageKey, nonce, plaintext, AD)
Zero ephemeral private key, DH outputs, IKM, and message key

POST /conversations/<conversationId>/messages
  { senderId, recipientId, payload }
                                                Verify JWT and senderId
                                                Check sender and recipient participation
                                                Consume one-time prekey transactionally
                                                Store ciphertext payload
                                                Grant message access rows
                                                Hash payload via blockchain sidecar
```

The payload stored by the server contains the public fields required for decryption: sender identity public key, sender ephemeral public key, signed prekey ID, optional one-time prekey ID, nonce, and ciphertext. The plaintext and message key never leave the client.

### 3.4 Receiving a Message

When `MainWindow::loadMessages()` receives encrypted payloads, it obtains or reuses the sender's pinned public key bundle and calls `LocalKeyStore::decryptMessage()`.

```text
Recipient
---------
Parse payload and check algorithm/version
Fetch or use pinned sender bundle
Validate sender signed prekey signature
Require payload senderIdentityPublicKey == pinned sender identity key
Find recipient signed prekey by signedPreKeyId
Find recipient one-time prekey by oneTimePreKeyId, if present

DH1 = X25519(recipient signed prekey private, sender identity public)
DH2 = X25519(recipient identity private, sender ephemeral public)
DH3 = X25519(recipient signed prekey private, sender ephemeral public)
DH4 = X25519(recipient one-time prekey private, sender ephemeral public), if present

Derive the same messageKey with HKDF-SHA256
Reconstruct the same AD JSON
Decrypt and authenticate with XChaCha20-Poly1305
If authentication fails, reject the message
If a one-time prekey was used, erase it locally and record its ID as used
```

Successful decryption proves possession of the private keys that correspond to the public bundle used by the sender, and the AEAD associated data proves that the ciphertext was intended for this exact conversation and recipient context.

### 3.5 Local Storage at Rest

Kingdom has two local encrypted stores.

The private-key store is `~/.kingdom/keys/<username>.json`. It uses:

```text
Argon2id13(password, 16-byte random salt,
           OPSLIMIT_MODERATE, MEMLIMIT_MODERATE)
HKDF-SHA256(info = "kd-key-encryption-v1", salt = base64(salt), L = 32)
XChaCha20-Poly1305(AD = username)
```

The local plaintext message cache is `~/.kingdom/messages/<username>.json`. It exists so already-decrypted messages can be displayed without repeatedly decrypting every poll. It uses a separate derivation context:

```text
Argon2id13(password, 16-byte random salt,
           OPSLIMIT_MODERATE, MEMLIMIT_MODERATE)
HKDF-SHA256(info = "kd-message-cache-encryption-v1", salt = salt bytes, L = 32)
XChaCha20-Poly1305(AD = "kingdom-message-cache-v2" || NUL || username)
```

Both files are written with owner-only read/write permissions where the filesystem supports it. The two HKDF info strings are deliberately different, so a key derived for private-key encryption cannot be confused with a key derived for message-cache encryption.

## 4. Primitive and Parameter Justification

### 4.1 X25519 for Diffie-Hellman

- Implementation: libsodium `crypto_box_keypair` for X25519-compatible keypairs and `crypto_scalarmult` for Diffie-Hellman.
- Parameters: 32-byte private scalar, 32-byte public Montgomery u-coordinate, approximately 128-bit classical security.
- Security property: Computational Diffie-Hellman hardness on Curve25519.
- Justification: X25519 is standardized in RFC 7748 Section 5 and is widely used in modern secure messaging protocols. The Montgomery ladder is designed for constant-time scalar multiplication, reducing timing side-channel risk. It is appropriate here because Kingdom needs compact public keys, fast client-side key agreement, and mature library support.

### 4.2 Ed25519 for Signed Prekeys

- Implementation: libsodium `crypto_sign_keypair`, `crypto_sign_detached`, and `crypto_sign_verify_detached`.
- Parameters: 32-byte public key, 64-byte secret key, 64-byte signature, approximately 128-bit classical security.
- Security property: unforgeability of the signed prekey authorization under the user's signing private key.
- Justification: Ed25519 is specified in RFC 8032 Section 5.1. It gives deterministic signatures, avoiding ECDSA-style nonce failure risks. Kingdom uses it only to bind the X25519 signed prekey to the user's signing key; message authentication itself comes from the authenticated key agreement plus AEAD.

### 4.3 HKDF-SHA256

- Implementation: OpenSSL EVP KDF with `HKDF` and digest `SHA256`.
- Output length: 32 bytes for AEAD keys.
- Message-key context: `info = "kd-x3dh-message-key-v1"`, `salt = "kd-x3dh-session-salt-v1"`.
- Private-key context: `info = "kd-key-encryption-v1"`, salt derived from the local random salt.
- Message-cache context: `info = "kd-message-cache-encryption-v1"`, salt equal to the local random salt bytes.
- Security property: HKDF extracts and expands shared secret material into uniformly distributed keys and gives domain separation through `info` strings.
- Justification: RFC 5869 Section 2 defines HKDF as an extract-then-expand KDF. RFC 5869 Section 3.1 allows non-secret salts; the entropy in the message-key use comes from the X25519 outputs, while the fixed salt and explicit info string separate this protocol from other derivations. Separate info strings prevent cross-use of keys between message encryption, private-key encryption, and message-cache encryption.

### 4.4 XChaCha20-Poly1305 AEAD

- Implementation: libsodium `crypto_aead_xchacha20poly1305_ietf_encrypt` and `_decrypt`.
- Parameters: 256-bit key, 192-bit nonce, 128-bit authentication tag.
- Security property: confidentiality plus ciphertext integrity/authenticity, assuming a nonce is not reused with the same key.
- Nonce strategy: every encryption generates a fresh 24-byte nonce from libsodium `randombytes_buf`.
- Justification: ChaCha20-Poly1305 is specified in RFC 8439. XChaCha20-Poly1305 extends the nonce to 192 bits, making random nonce generation safe for practical message volumes. Kingdom also derives a fresh message key for each sent payload because each send uses a new ephemeral X25519 keypair, so nonce reuse under the same key is already extremely unlikely; the 192-bit nonce is additional safety.

### 4.5 Associated Data

Message AEAD associated data contains protocol version, algorithm, conversation ID, sender ID, recipient ID, sender identity public key, sender ephemeral public key, recipient identity public key, signed prekey ID, and optional one-time prekey ID.

This matters because the server is untrusted for ciphertext integrity. Without AD, a server could copy a valid ciphertext into a different conversation, attribute it to a different sender, or target a different recipient context. With AD, those changes produce a different authentication input and decryption fails.

Local store AEAD also uses associated data. The key file uses the username as AD. The message cache uses `kingdom-message-cache-v2 || NUL || username`. This prevents encrypted local blobs from being silently transplanted into a different account context under the same password.

### 4.6 Argon2id Password Hashing and Local KDFs

Server-side passwords:

- Implementation: libsodium `crypto_pwhash_str_alg` and `crypto_pwhash_str_verify`.
- Algorithm: Argon2id13.
- Parameters: `crypto_pwhash_OPSLIMIT_INTERACTIVE` and `crypto_pwhash_MEMLIMIT_INTERACTIVE`.
- Password policy: signup requires 12-72 characters, at least one uppercase character, and at least one number. Login rejects passwords above 72 characters to avoid ambiguous handling of very long passwords.
- Justification: Argon2id is recommended by RFC 9106 because it balances resistance to GPU cracking and side-channel concerns. Interactive parameters are appropriate server-side because login must be responsive and the server must resist denial-of-service from many simultaneous attempts.

Client local stores:

- Implementation: libsodium `crypto_pwhash` with Argon2id13, then HKDF-SHA256, then XChaCha20-Poly1305.
- Parameters: `crypto_pwhash_OPSLIMIT_MODERATE` and `crypto_pwhash_MEMLIMIT_MODERATE`.
- Justification: local private keys and local plaintext cache files are attacked offline if the laptop or home directory is stolen. Moderate parameters are more expensive than interactive login parameters and are acceptable because the derivation runs once when unlocking the client session, not per message.

### 4.7 CSPRNG and Secret Clearing

All keypairs, nonces, and salts come from libsodium functions backed by the operating system CSPRNG (`crypto_box_keypair`, `crypto_sign_keypair`, and `randombytes_buf`). The code clears several sensitive buffers with `sodium_memzero`, including Argon2 intermediate secrets, HKDF output keys, ephemeral private keys, DH outputs, decrypted local private-material JSON, and GUI password strings after login/signup. Some long-lived private keys remain in memory for the duration of the logged-in client session because they are required to decrypt and send messages.

### 4.8 JWT Session Tokens

JWTs are authentication tokens, not part of the E2EE message encryption, but they protect server API access.

- Implementation: HMAC-SHA256 using OpenSSL `HMAC(EVP_sha256())`.
- JWT header: `alg = HS256`, `typ = JWT`.
- Claims: `sub`, `username`, `iat`, `exp`.
- Server secret: `KD_JWT_SECRET` must be at least 32 characters.
- Verification: signature comparison uses `CRYPTO_memcmp` to avoid timing leaks.
- Revocation: logout stores the token in an in-memory blacklist. This blacklist is lost on server restart, so token expiry remains the durable bound.

## 5. Protocol Citations

| Topic | Reference | Kingdom usage |
| --- | --- | --- |
| X3DH | Marlinspike and Perrin, The X3DH Key Agreement Protocol, Signal, Sections 2-4 | Overall prekey bundle model and 3-DH/4-DH authenticated key agreement pattern. Simplified: no full Signal double ratchet and no automated signed-prekey rotation. |
| X25519 | RFC 7748, Section 5 | Diffie-Hellman primitive for identity keys, signed prekeys, one-time prekeys, and ephemeral sender keys. |
| Ed25519 | RFC 8032, Section 5.1 | Signature over the signed prekey. |
| HKDF | RFC 5869, Sections 2 and 3.1 | Deriving AEAD keys from DH outputs and Argon2id outputs with explicit salt and info strings. |
| ChaCha20-Poly1305 | RFC 8439 | AEAD basis for XChaCha20-Poly1305. |
| XChaCha20-Poly1305 | libsodium documentation for `crypto_aead_xchacha20poly1305_ietf` | Message payload encryption, private-key file encryption, and local message-cache encryption. |
| Argon2id | RFC 9106, especially Section 4 | Server password hashing and local password-based key derivation. |
| JWT | RFC 7519 | Session-token claim structure and expiry semantics. |
| HMAC | RFC 2104 | HS256 JWT signature construction. |

## 6. Known Limitations

1. **TOFU cannot authenticate first contact.** The first key bundle returned by the server for a user is trusted and pinned. If the server is compromised before first contact, it can provide an attacker-controlled bundle.

2. **No PKI or out-of-band identity binding.** The GUI can show a fingerprint for forwarding, and the client pins keys after first use, but there is no certificate authority, web-of-trust, or mandatory QR-code style verification for every conversation.

3. **No Double Ratchet.** Kingdom performs an X3DH-style setup per message send using a fresh sender ephemeral key, but it does not implement the Signal Double Ratchet. There is no per-conversation ratchet state, skipped-message key handling, or post-compromise recovery.

4. **Signed prekey rotation is not automated.** Each local identity has signed prekey ID 1. The X3DH specification recommends periodic signed-prekey rotation. If the signed prekey private key is compromised, sessions using that prekey are weakened.

5. **One-time prekey replenishment is not implemented.** Signup creates 20 one-time prekeys. The server removes a used prekey from the public bundle transactionally, and the recipient records used IDs locally after decryption. Once the batch is exhausted, new messages fall back to the three-DH variant without DH4.

6. **Same-context replay is not fully solved at the E2EE layer.** Associated data prevents replay into a different conversation or recipient context, but the ciphertext format does not include a sender monotonic counter. The server assigns unique message IDs, but a malicious server controls that layer.

7. **Group messaging is not full multi-recipient E2EE.** The client encrypts one payload to one recipient ID. For a group conversation, a production design would send a separately encrypted payload for each recipient or use a group messaging protocol. The current implementation is strongest for one-to-one conversations.

8. **Local plaintext cache exists by design.** Decrypted messages are cached locally for usability and polling performance. The cache is encrypted at rest with a separate password-derived key, but plaintext exists in client memory while the user is logged in.

9. **JWT revocation is in memory.** Logout blacklists the token only until the server process restarts. Token expiry (`KD_JWT_TTL_SECONDS`, default 3600 seconds) is the durable session bound.

10. **Availability is outside the cryptographic guarantee.** A malicious server can refuse login, hide messages, delete database rows, return stale public key bundles, or decline to record blockchain hashes. Cryptography detects tampering with ciphertext contents but cannot force delivery.

## 7. Demo Evidence for the Cryptography Requirements

- The database `messages.payload` column contains only JSON encrypted payloads with nonce and ciphertext, not plaintext.
- Modifying `payload.ciphertext`, `payload.nonce`, `conversationId`, `senderId`, `recipientId`, or key fields causes client decryption failure because the AEAD tag no longer verifies.
- The local key file stores encrypted private material under `privateMaterial.ciphertext`; private keys are not present in plaintext.
- The local message cache is version 2 and stores a ciphertext envelope, not raw decrypted message text.
- Changing a user's pinned `identityKey` or `signingKey` in a later server response causes the client to refuse the key because it conflicts with `known_keys.json`.
- Server password rows contain Argon2id encoded hashes, not user passwords.
