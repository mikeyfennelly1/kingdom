# Cryptographic Design Document — Kingdom

CS4455 Cybersecurity | Cryptography (Eoin O'Brien) | 2026

---

## 1. Overview

Kingdom provides end-to-end encrypted messaging using a simplified variant of the X3DH (Extended Triple Diffie-Hellman) key agreement protocol, combined with XChaCha20-Poly1305 authenticated encryption. The server relays and stores ciphertext only; it has no access to message plaintexts. Passwords are protected server-side using Argon2id; private keys are protected at rest on the client under a key derived from the user's password.

The implementation is in C++20 using libsodium 1.0.x and OpenSSL 3.0+ via their respective EVP interfaces. No cryptographic primitives are hand-rolled.

---

## 2. Threat Model

### 2.1 Attacker Classes

**Class (a) — Passive network attacker**
An attacker who can read all network traffic between clients and the server, but cannot modify it.

| Property | Held? | Basis |
|----------|-------|-------|
| Message confidentiality | Yes | TLS 1.3 transport encryption + XChaCha20-Poly1305 E2EE |
| Message integrity | Yes | Poly1305 AEAD MAC + TLS MAC |
| Sender authenticity | Yes | TOFU identity key binding in associated data |
| Traffic metadata (who, when) | No | Timing and participant identity visible in TLS records |

**Class (b) — Active network attacker**
An attacker who can additionally modify, drop, replay, or inject network traffic.

| Property | Held? | Basis |
|----------|-------|-------|
| Message confidentiality | Yes | TLS cert verification enabled; E2EE unaffected by transport layer |
| Ciphertext tamper detection | Yes | Poly1305 MAC covers ciphertext; AEAD decryption fails on modification |
| Relay / reorder attacks | Yes | Associated data (AD) binds ciphertext to conversationId, senderId, recipientId, ephemeral key, and prekey IDs; decryption fails under the wrong context |
| Replay of stored ciphertext | Partial | Random 192-bit nonces prevent accidental reuse; no server-side sequence number enforcement |
| TLS MITM | Yes | Client enables `SSL_VERIFY_PEER`; certificate pinned to CA cert at runtime |

**Class (c) — Honest-but-curious server**
The server faithfully executes the protocol but logs and analyses all data it can observe.

| Property | Held? | Basis |
|----------|-------|-------|
| Message plaintext confidentiality | Yes | Server stores and forwards ciphertext only |
| Tamper detection | Yes | Server cannot modify ciphertext without detection by the recipient's AEAD verification |
| Password confidentiality | Yes | Argon2id hash stored; plaintext not recoverable from database |
| Private key confidentiality | Yes | Client never transmits private keys; private material stays in `~/.kingdom/keys/` |
| Conversation metadata | No | Server knows participant IDs, conversation IDs, timestamps, and message sizes |

**Class (d) — Fully compromised server**
The attacker controls the server, has read/write access to the database, and can send arbitrary responses to clients.

| Property | Held? | Basis |
|----------|-------|-------|
| Past ciphertext confidentiality | Yes | AEAD ciphertext in database requires recipient private key to decrypt |
| Stored ciphertext integrity | Yes | Any modification will fail AEAD verification on the client |

**Properties NOT held against a fully compromised server:**

1. **Future message confidentiality.** The server can serve a malicious public key bundle under any user's ID. A sender who has not previously pinned the victim's key (first contact) will encrypt to the attacker's key. All such messages are readable by the attacker.

2. **Sender authentication for new sessions.** A compromised server can substitute key bundles, enabling impersonation of any participant in a newly established session.

3. **Forward secrecy beyond the initial exchange.** The design uses a single X3DH key exchange per conversation without a subsequent double ratchet. Compromise of a session key or long-term private key does not automatically protect past messages in that session if the key material is still held.

4. **Protection against message suppression or selective delivery.** The server can drop, reorder, or selectively withhold messages without detection by the clients.

The design does not implement TOFU key updates: once a key is pinned, a server-substituted key is rejected by the receiver, providing some protection for established contacts. However, first-contact sessions against a compromised server have no such protection.

---

## 3. Construction Walkthrough

### 3.1 Registration and Key Publication

At registration, the client generates a key bundle and uploads it to the server alongside the user's credentials. The server stores the hash of the password and the public key bundle.

```
Client (kdctl)                                     Server (kds)
──────────────────────────────────────────────────────────────
LocalKeyStore::createForSignup(username, password)

  Generate X25519 identity keypair      →  IK_pub, IK_priv
  Generate Ed25519 signing keypair      →  SK_pub, SK_priv
  Generate X25519 signed prekey         →  SPK_pub, SPK_priv
  Sign SPK_pub under SK_priv (Ed25519)  →  SPK_sig
  Generate 20 × X25519 one-time prekeys →  OPK_1..20

  Derive key-encryption key:
    salt  ← randombytes(16)
    pwd_secret ← Argon2id(password, salt,
                          OPSLIMIT_MODERATE, MEMLIMIT_MODERATE)
    KEK ← HKDF-SHA256(pwd_secret,
                      info="kd-key-encryption-v1",
                      salt=base64(salt))

  Encrypt private material:
    nonce ← randombytes(24)
    private_json ← JSON{IK_priv, SK_priv, SPK, OPKs}
    ciphertext ← XChaCha20-Poly1305(key=KEK,
                                    nonce=nonce,
                                    plaintext=private_json,
                                    AD=username)
  Save to ~/.kingdom/keys/<username>.json

  POST /signup  { username, Argon2id(password), public_bundle }
                                               ──────────────────►
                                               Argon2id(password,
                                               OPSLIMIT_INTERACTIVE,
                                               MEMLIMIT_INTERACTIVE)
                                               → stored hash
                                               Store public_bundle
                                               Issue JWT (HS256)
                                               ◄──────────────────
  Receive JWT, store in memory
```

**Public bundle format (JSON, `KD-X3DH-BUNDLE-V1`):**

```json
{
  "version": 1,
  "algorithm": "KD-X3DH-BUNDLE-V1",
  "identityKey":  "<base64(IK_pub, 32 bytes)>",
  "signingKey":   "<base64(SK_pub, 32 bytes)>",
  "signedPreKey": {
    "id":        1,
    "publicKey": "<base64(SPK_pub, 32 bytes)>",
    "signature": "<base64(Ed25519_detached(SPK_sig), 64 bytes)>"
  },
  "oneTimePreKeys": [
    { "id": 1, "publicKey": "<base64(OPK1_pub)>" },
    ...
  ]
}
```

### 3.2 Sending a Message

The sender fetches the recipient's key bundle (TOFU check performed on first contact) and performs the X3DH key agreement. This yields a 256-bit message key, under which the plaintext is encrypted with XChaCha20-Poly1305.

```
Sender (kdctl)                                     Server (kds)
──────────────────────────────────────────────────────────────
GET /users/{recipientId}/public-key
                                               ◄──────────────────
  recipient_bundle (JSON)

  Validate bundle:
    - version == 1, algorithm == "KD-X3DH-BUNDLE-V1"
    - verify SPK signature: Ed25519.verify(SK_pub, SPK_pub, SPK_sig)
    - TOFU check: if known_keys.json has entry for recipientId,
        assert identityKey == pinned value

  Generate ephemeral X25519 keypair:  EK_pub, EK_priv

  X3DH key agreement (4 DH operations):
    DH1 = X25519(sender.IK_priv,   recipient.SPK_pub)
    DH2 = X25519(EK_priv,          recipient.IK_pub)
    DH3 = X25519(EK_priv,          recipient.SPK_pub)
    DH4 = X25519(EK_priv,          recipient.OPK_pub)  [if available]

  IKM = DH1 || DH2 || DH3 || DH4

  message_key ← HKDF-SHA256(
    IKM,
    salt = "kd-x3dh-session-salt-v1",
    info = "kd-x3dh-message-key-v1",
    L    = 32 bytes)

  sodium_memzero(EK_priv, DH1..DH4, IKM)

  Construct associated data (AD) JSON:
    { version, algorithm, conversationId, senderId, recipientId,
      senderIdentityPublicKey, senderEphemeralPublicKey,
      recipientIdentityPublicKey, signedPreKeyId, oneTimePreKeyId }

  nonce ← randombytes(24)

  ciphertext ← XChaCha20-Poly1305.encrypt(
    key       = message_key,
    nonce     = nonce,
    plaintext = message_text,
    AD        = AD.dump())

  sodium_memzero(message_key)

  payload = JSON {
    version, algorithm,
    senderIdentityPublicKey, senderEphemeralPublicKey,
    signedPreKeyId, oneTimePreKeyId,
    nonce, ciphertext
  }

  POST /conversations/{convId}/messages
       { senderId, payload }
                                               ──────────────────►
                                               Validate JWT, senderId
                                               Check participant
                                               Store ciphertext (payload)
                                               Call blockchain sidecar
                                               Return { msgId, txHash }
```

### 3.3 Receiving a Message

```
Recipient (kdctl)                                  Server (kds)
──────────────────────────────────────────────────────────────
GET /conversations/{convId}/messages
                                               ◄──────────────────
  [{ id, senderId, payload, timestamp, txHash }, ...]

  For each message from a sender not yet cached:
    GET /users/{senderId}/public-key  →  sender_bundle
    TOFU check / pin sender identity key

  LocalKeyStore::decryptMessage(payload, identity, sender_bundle,
                                conversationId, senderId, recipientId)

    Parse payload JSON; validate version and algorithm
    Verify senderIdentityPublicKey matches pinned sender bundle

    Look up recipient.SPK by signedPreKeyId
    Look up recipient.OPK by oneTimePreKeyId (if present)

    X3DH key agreement (mirror of sender):
      DH1 = X25519(recipient.SPK_priv, sender.IK_pub)
      DH2 = X25519(recipient.IK_priv,  sender.EK_pub)
      DH3 = X25519(recipient.SPK_priv, sender.EK_pub)
      DH4 = X25519(recipient.OPK_priv, sender.EK_pub)  [if used]

    IKM = DH1 || DH2 || DH3 || DH4

    message_key ← HKDF-SHA256(IKM,
      salt = "kd-x3dh-session-salt-v1",
      info = "kd-x3dh-message-key-v1",
      L    = 32 bytes)

    Reconstruct AD (same fields as encryption)

    plaintext ← XChaCha20-Poly1305.decrypt(
      key       = message_key,
      nonce     = nonce,
      ciphertext = ciphertext,
      AD        = AD.dump())
    → throws on authentication failure (tamper detected)

    sodium_memzero(message_key, DH1..DH4, IKM)

    If oneTimePreKeyId present: erase OPK from local store
      (sodium_memzero on private key, record ID in usedOneTimePreKeyIds)

  Display plaintext
```

### 3.4 Storage at Rest

Private key material on the client is never stored in plaintext. The local key file at `~/.kingdom/keys/<username>.json` contains only the encrypted private bundle:

```
password
   │
   ▼
Argon2id(password, salt_16B, OPSLIMIT_MODERATE, MEMLIMIT_MODERATE)
   │ 32-byte secret
   ▼
HKDF-SHA256(secret, salt=base64(salt), info="kd-key-encryption-v1")
   │ 32-byte KEK (Key Encryption Key)
   ▼
XChaCha20-Poly1305.encrypt(
  key   = KEK,
  nonce = randombytes(24),
  msg   = JSON{IK_priv, SK_priv, SPK, OPK_1..20},
  AD    = username)
   │
   ▼
Stored: { ciphertext, nonce, salt, opsLimit, memLimit }
```

The key file has permissions `0600` (owner read/write only). The username is included as associated data, preventing the encrypted blob from being transferred to a different account and decrypted under the same password.

On the server, passwords are stored as Argon2id encoded hashes (libsodium `crypto_pwhash_str_alg` format, which embeds the salt, algorithm identifier, and parameters in the hash string). Plaintexts are never logged or stored.

---

## 4. Primitive Justification

### 4.1 Curve25519 / X25519 (DH key agreement)

- **Algorithm:** X25519, the Diffie-Hellman function over Curve25519 (RFC 7748, Section 5).
- **Parameters:** 255-bit prime field; private keys are 32 bytes with clamping applied by libsodium's `crypto_scalarmult`. Public keys are 32-byte Montgomery u-coordinates.
- **Security property relied upon:** Computational Diffie-Hellman (CDH) hardness on Curve25519. No known sub-exponential attacks; estimated ~128-bit security level.
- **Justification:** Curve25519 was designed with resistance to implementation errors as an explicit goal (Bernstein, 2006). Unlike NIST P-256, the curve parameters are verifiably not generated in a way that could embed a backdoor. The Montgomery ladder used for scalar multiplication is constant-time by construction, preventing timing side-channels. libsodium's `crypto_scalarmult` wraps this correctly. 128-bit security is appropriate for the deployment lifetime of this application.

### 4.2 Ed25519 (signatures)

- **Algorithm:** Ed25519 (Bernstein et al., 2011), using SHA-512 internally and the Edwards curve edwards25519.
- **Parameters:** 255-bit Edwards curve; private key 64 bytes (seed + public), public key 32 bytes, signature 64 bytes.
- **Security property relied upon:** Existential unforgeability under chosen-message attack (EUF-CMA), ~128-bit security.
- **Justification:** Used only for signing the signed prekey, establishing that the holder of the identity signing key authorised that prekey. Ed25519 provides deterministic signatures (no per-signature randomness required, eliminating nonce-reuse risk present in ECDSA). libsodium's `crypto_sign_detached` and `crypto_sign_verify_detached` are used directly.
- **Signature input domain separation:** The signed prekey signature covers `"kd-x3dh-signed-prekey-signature-v1" || uint64_le(preKeyId) || preKey_pub` — the domain separator prevents cross-context signature reuse.

### 4.3 XChaCha20-Poly1305 (AEAD encryption)

- **Algorithm:** XChaCha20-Poly1305 (libsodium `crypto_aead_xchacha20poly1305_ietf`), based on the IETF ChaCha20-Poly1305 construction (RFC 8439) with an extended 192-bit nonce.
- **Parameters:** 256-bit key; 192-bit (24-byte) nonce; 128-bit (16-byte) Poly1305 authentication tag.
- **Security properties relied upon:** IND-CPA confidentiality under ChaCha20 (stream cipher); INT-CTXT integrity and authenticity under Poly1305 (one-time MAC with universal hash). The AEAD combination provides IND-CCA2 security assuming the nonce is not reused under the same key.
- **Justification for XChaCha20 over ChaCha20-Poly1305:** The IETF ChaCha20-Poly1305 (RFC 8439) uses a 96-bit nonce, which is unsafe to generate randomly at scale (birthday collision probability becomes non-negligible at around 2^48 messages per key). XChaCha20 extends the nonce to 192 bits by using the first 128 bits in an HChaCha20 sub-key derivation, making random nonce generation safe for any practical message volume under a single key. Since message keys in this design are single-use (derived per session via X3DH), random nonces are safe regardless, but the extended nonce provides defence-in-depth.
- **Nonce strategy:** Nonces are generated fresh via libsodium's `randombytes_buf` (a CSPRNG backed by the OS). Each message key is derived independently per session; no key is used for more than one message in the current design, so nonce collision across sessions is impossible.
- **Associated data:** The AD JSON covers `conversationId`, `senderId`, `recipientId`, both identity public keys, the ephemeral public key, and the prekey IDs. This binding ensures a ciphertext cannot be replayed into a different conversation, attributed to a different sender, or decrypted under a different key setup without triggering an authentication failure.

### 4.4 HKDF-SHA256 (key derivation)

- **Algorithm:** HMAC-based Key Derivation Function (HKDF), RFC 5869, using SHA-256 as the hash.
- **Parameters:** Output length 32 bytes (256 bits) for all uses; fixed info strings per context; salt as described below.
- **Security property relied upon:** Pseudorandom output indistinguishable from random given a uniformly distributed input, under the assumption that HMAC-SHA256 is a pseudorandom function (PRF).
- **X3DH message key:** Salt is the fixed ASCII string `"kd-x3dh-session-salt-v1"`, info is `"kd-x3dh-message-key-v1"`. A fixed salt is explicitly permitted by RFC 5869, Section 3.1, which states that a non-secret salt is better than no salt and provides domain separation. The IKM (input key material) is the concatenation of the four DH outputs, which carries the session entropy; the salt does not need to be random.
- **Local key encryption:** A fresh random 16-byte salt (from `randombytes_buf`) is used for each key file, stored alongside the ciphertext. Info is `"kd-key-encryption-v1"`. The salt is random here because HKDF is applied to an Argon2id output which, while high-entropy, benefits from a per-file salt for domain separation.
- **Domain separation:** Distinct info strings are used for every derivation context (`"kd-key-encryption-v1"`, `"kd-x3dh-message-key-v1"`), ensuring that the same input key material cannot be confused across purposes.

### 4.5 Argon2id (password hashing and key derivation)

Two separate uses of Argon2id are made, with intentionally different parameters:

**Server-side password verification (login/signup):**
- Function: libsodium `crypto_pwhash_str_alg` with `crypto_pwhash_ALG_ARGON2ID13`
- `OPSLIMIT_INTERACTIVE` = 2 iterations
- `MEMLIMIT_INTERACTIVE` = 67,108,864 bytes (64 MiB)
- Output: encoded hash string (embeds algorithm, salt, parameters)
- **Justification:** `OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE` are libsodium's recommended minimum for interactive logins where the user expects a short response time (~0.5 s on typical hardware). The 64 MiB memory requirement renders GPU/ASIC parallelism costly. Using higher parameters would create a denial-of-service risk on the server (parallel login attempts would exhaust memory). Argon2id is chosen over Argon2d (timing attack resistance) and Argon2i (better GPU resistance); Argon2id combines both, as recommended by RFC 9106, Section 4.

**Client-side key-encryption key derivation (key file at rest):**
- Function: libsodium `crypto_pwhash` with `crypto_pwhash_ALG_ARGON2ID13`
- `OPSLIMIT_MODERATE` = 3 iterations
- `MEMLIMIT_MODERATE` = 268,435,456 bytes (256 MiB)
- Output: 32-byte raw secret (input to HKDF)
- **Justification:** The client derives a key-encryption key when unlocking the local key file (at login). This is done once per session on the user's own device, so a higher-cost parameterisation is appropriate and acceptable. `OPSLIMIT_MODERATE` / `MEMLIMIT_MODERATE` is 4× the memory of the interactive preset, significantly increasing the cost of offline dictionary attacks against a stolen key file, without impacting normal usage (executed once at login, not per-message). The parameters are stored in the key file, allowing future migration to stronger settings.
- **Separation from server-side parameters:** The two uses of Argon2id are distinct and non-interchangeable: they operate on the same password but with different salts, parameter sets, and output lengths, and the outputs are used for entirely different purposes. A database breach exposing the server-side hash does not reveal the client-side KEK and vice versa.

### 4.6 HS256 / HMAC-SHA256 (session tokens)

- **Algorithm:** HMAC-SHA256 (RFC 2104) used as the JWT signing algorithm (RFC 7519 / RFC 7515, `alg=HS256`).
- **Parameters:** JWT claims include `sub` (user ID), `username`, `iat` (issued-at epoch), `exp` (expiry epoch). Server secret minimum 32 bytes, enforced at startup.
- **Security property:** MAC unforgeability; a token cannot be forged without knowledge of the server secret.
- **Signature verification:** Uses OpenSSL's `CRYPTO_memcmp` for constant-time comparison, preventing timing oracle attacks on the signature.
- **Token revocation:** Revoked tokens are placed in an in-memory `TokenBlacklist` (thread-safe). This blacklist is lost on server restart; tokens remain valid until expiry if the server restarts. This is a known limitation (see Section 5).
- **Limitation:** HS256 is a symmetric scheme — the server can both produce and verify tokens. A compromised server can forge arbitrary tokens. This is acceptable given that server compromise is already in scope for threat class (d), under which authentication properties are not guaranteed.

---

## 5. Protocol Citations

| Protocol / Specification | Reference | Usage |
|--------------------------|-----------|-------|
| X3DH Key Agreement Protocol | Marlinspike & Perrin, "The X3DH Key Agreement Protocol", Signal, Rev. 1 (2016). §2–§3 | Key agreement structure; DH sequence; prekey bundle format. Simplified: no identity key type distinction (same curve for IK and EK), no associated data in the spec's KDF; KDF construction diverges (see §3.2 above). |
| HKDF | RFC 5869, Krawczyk & Eronen (2010). §2 (extract-then-expand), §3.1 (salt selection) | Key derivation from X3DH DH outputs and from Argon2id output |
| XChaCha20-Poly1305 | Bernstein, "ChaCha, a variant of Salsa20" (2008); RFC 8439 (IETF ChaCha20-Poly1305); libsodium documentation on `crypto_aead_xchacha20poly1305_ietf` | AEAD encryption of messages and private material at rest |
| Argon2id | RFC 9106, Biryukov et al. (2021). §4 (parameter selection) | Password hashing (server) and key encryption (client) |
| Curve25519 / X25519 | RFC 7748, Langley, Hamburg & Turner (2016). §5 | DH function for all key agreement operations |
| Ed25519 | RFC 8032, Josefsson & Liusvaara (2017). §5.1 | Signed prekey signature; identity signing key |
| JWT | RFC 7519, Jones et al. (2015). §4.1 (registered claims), §7.1 (validation) | Session tokens |
| HMAC | RFC 2104, Krawczyk, Bellare & Canetti (1997) | JWT signing (HS256) |

---

## 6. Known Limitations

1. **No forward secrecy ratchet.** The design uses a single X3DH exchange per conversation. There is no double ratchet (Signal Protocol) to derive per-message keys that are deleted after use. If the session key or a long-term private key is compromised, all messages encrypted under that session key may be at risk. The design is therefore a static Diffie-Hellman-based scheme with one-time prekey enhancement, not a forward-secret ratchet.

2. **Static signed prekey.** The signed prekey ID is always 1 and is never rotated in the current implementation. The X3DH specification (§4.4) recommends periodic rotation to improve forward secrecy properties. A compromised signed prekey private key allows decryption of all sessions established using it.

3. **TOFU does not protect first-contact sessions against a compromised server.** Key pinning via `known_keys.json` only protects existing contacts. A first-contact session is always vulnerable to a key substitution attack by a compromised server.

4. **Group message encryption is pairwise.** The API encrypts one payload per `(sender, recipient)` pair. In a group conversation with N participants, the sender must call `encryptMessage` N-1 times. The current `kdctl` implementation sends a single payload per message, meaning that in a group conversation only one recipient can correctly decrypt. This is a known gap in the current implementation that would require per-recipient payloads to be fully correct.

5. **Token blacklist is in-memory.** Revoked JWT tokens are stored in a `std::unordered_set` in the `Controller` process. A server restart clears this list. Tokens remain usable until their `exp` claim expires. The TTL should therefore be kept short (configured via `KD_JWT_TTL_SECONDS`).

6. **No one-time prekey replenishment.** When one-time prekeys are consumed, the client does not automatically upload a new batch. Once the 20 prekeys are exhausted, new sessions will fall back to the 3-DH variant (omitting DH4), reducing the deniability properties of the initial key exchange.

7. **Static HKDF salt for message key derivation.** The X3DH message key HKDF uses a fixed ASCII salt (`"kd-x3dh-session-salt-v1"`) rather than a random per-session salt or the structured input recommended by the Signal X3DH spec. This is safe — RFC 5869 §3.1 explicitly permits a non-secret salt, and the entropy comes from the DH outputs — but it is a deviation from the Signal reference.

8. **Password truncation at 72 bytes.** Argon2id in libsodium passes the password to bcrypt-derived internals with a 72-byte limit. Passwords longer than 72 bytes are silently truncated. The server enforces a hard 72-byte maximum with a 400 error to prevent user confusion, but this is a libsodium implementation constraint that limits password space for very long passphrases.
