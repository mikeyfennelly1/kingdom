# TLS / HTTPS Implementation Plan

## Marking Context

This work falls under **Computer Networks & Cybersecurity (Mark Burkley)** — 40 marks total, four criteria at 10 marks each:

| Criterion | What's needed |
|---|---|
| Network coding using sockets API | TLS connection establishment, hostname resolution |
| Crypto coding + SSL certificate verification | `httplib::SSLClient`, cert validation |
| Secure coding and input validation | Input sanitisation, CSPRNG tokens, no plaintext secrets |
| Pentest + known vulnerabilities | Pentest report, OWASP checklist |

The spec says: *"front-end programs should create SSL-protected connections"*, *"client must verify authenticity and validity of SSL certificate"*, and notes that using low-level `libssl/libcrypto` directly is *"impressive"* but `cpp-httplib` is acceptable.

---

## Current State

| Component | Status |
|---|---|
| Server (`kds`) | Plain `httplib::Server` — no TLS |
| Client (`libkd/Client.cc`) | Plain `httplib::Client` — no cert verification |
| CMake | OpenSSL available but not linked for TLS use |
| Session tokens | `std::mt19937_64` — PRNG, not CSPRNG |
| Blockchain sidecar calls | Plain HTTP |

---

## Step 1 — Wire OpenSSL into CMake

### `cmake/dependencies.cmake`

`cpp-httplib` is already fetched but needs the `CPPHTTPLIB_OPENSSL_SUPPORT` compile definition and an explicit OpenSSL link. Add after the `FetchContent_MakeAvailable(httplib)` call:

```cmake
find_package(OpenSSL 3.0 REQUIRED)
target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto)
```

### `kds/CMakeLists.txt`

Add OpenSSL to the server target:

```cmake
target_link_libraries(kds PRIVATE OpenSSL::SSL OpenSSL::Crypto)
```

### `libkd/CMakeLists.txt`

Same for the shared library (used by kdctl):

```cmake
target_link_libraries(libkd PRIVATE OpenSSL::SSL OpenSSL::Crypto)
```

---

## Step 2 — Generate a TLS Certificate

For development/testing use a self-signed certificate. For a proper demo generate one for `localhost` with a Subject Alternative Name (SAN) so modern clients accept it.

```bash
# Run from project root — creates certs/server.key and certs/server.crt
mkdir -p certs
openssl req -x509 -newkey rsa:4096 -sha256 -days 365 -nodes \
  -keyout certs/server.key \
  -out certs/server.crt \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

Keep `certs/server.key` out of version control — add `certs/*.key` to `.gitignore`.

The cert path should be configurable (environment variable or CLI flag), not hardcoded.

---

## Step 3 — Server-Side TLS (`kds`)

### `Controller.hh`

Replace `httplib::Server` with `httplib::SSLServer`:

```cpp
#include <httplib.h>
// ...
httplib::SSLServer svr_;
```

### `Controller.cc` constructor

The `SSLServer` constructor takes cert and key paths:

```cpp
Controller::Controller(const std::string &host, int port,
                       const std::string &certPath,
                       const std::string &keyPath,
                       const std::string &sidecarUrl)
    : host_(host), port_(port),
      svr_(certPath.c_str(), keyPath.c_str()),
      sidecar_(sidecarUrl) {
    // ... existing route registration
}
```

The rest of the route registration code (`svr_.Post(...)`, `svr_.Get(...)`, `svr_.listen(...)`) stays the same — `SSLServer` has an identical API to `Server`.

### `kds/src/main.cc`

Pass cert/key paths from environment or config:

```cpp
const char *cert = std::getenv("KD_TLS_CERT");
const char *key  = std::getenv("KD_TLS_KEY");
if (!cert || !key) {
    spdlog::critical("KD_TLS_CERT and KD_TLS_KEY must be set");
    return 1;
}
Controller ctrl(host, port, cert, key, sidecarUrl);
```

---

## Step 4 — Client-Side TLS (`libkd/Client.cc`)

### Switch to `httplib::SSLClient`

Replace `httplib::Client` with `httplib::SSLClient`. The SSL client constructor takes the host only (port is set separately); TLS is implicit.

```cpp
#include <httplib.h>

// In Client::Client(std::string baseUrl):
// Parse host and port from baseUrl, then:
httplib::SSLClient cli(host, port);
cli.set_ca_cert_path(caPath);           // path to CA bundle or self-signed cert
cli.enable_server_certificate_verification(true);
cli.set_hostname_addr_map({{host, addr}}); // optional, for localhost testing
```

For the submission the CA path should point to the self-signed `server.crt` (acting as its own CA). In a real deployment it would point to the system CA bundle or the issuing CA cert.

### Certificate verification

The spec explicitly states the client **must verify** the certificate. Two things to check:

1. **Chain validation** — `cli.set_ca_cert_path(...)` handles this.
2. **Hostname verification** — enabled by default in cpp-httplib's SSLClient. Do not disable it.

If you want to do this at a lower level (which the spec calls *impressive*), you can call `SSL_CTX_set_verify()` and supply a custom `verify_callback` that checks `X509_check_host()`. This demonstrates understanding of the TLS handshake and would score well under "crypto coding using libssl".

---

## Step 5 — Fix Session Token Generation

The current code in `Controller.cc` uses `std::mt19937_64` which is a PRNG, not a CSPRNG. Session tokens derived from predictable RNG are a **Broken Authentication** vulnerability (OWASP A07).

Replace with libsodium (already linked) or OpenSSL RAND:

```cpp
// Using libsodium (already a dependency)
#include <sodium.h>

std::string generateSessionToken() {
    unsigned char buf[32];
    randombytes_buf(buf, sizeof(buf));
    // hex-encode for safe HTTP header transmission
    char hex[65];
    sodium_bin2hex(hex, sizeof(hex), buf, sizeof(buf));
    return std::string(hex);
}
```

---

## Step 6 — OWASP Controls Checklist

The spec requires checking these controls. Current status:

| OWASP Control | Status | Action needed |
|---|---|---|
| Improper Input Validation | Partial — JSON parsing exists | Validate all string fields for length/content before DB insert |
| Broken Authentication | Partial — Argon2id for passwords | Fix PRNG → CSPRNG for tokens (Step 5) |
| Broken Access Control | Partial — `ValidateAuthenticated` predicate | Ensure users can only access their own conversations/messages |
| Cryptographic Issues | Not done | TLS (this doc) + E2EE (crypto module) |
| Injection | Unknown | Use parameterised queries in all pqxx calls — check Controller.cc |
| Security Misconfiguration | Partial | Disable server headers (don't leak `httplib` version), enforce HTTPS-only |
| Sensitive Data Exposure | Currently bad | Passwords/tokens over plain HTTP — fixed by TLS |
| Vulnerable Components | N/A at submission | Note library versions in pentest report |

---

## Step 7 — Pentest Report

The spec requires a penetration testing report as part of the Networks & Cybersecurity mark. Minimum to cover:

1. **Pre-TLS findings** — show that credentials were transmitted in plaintext (Wireshark capture of HTTP POST to `/login`).
2. **Post-TLS verification** — show that the same capture is now TLS-encrypted.
3. **Certificate validation** — demonstrate the client rejects a connection to a server with an invalid/mismatched cert.
4. **Session token entropy** — note the PRNG issue if it existed and confirm the fix.
5. **Input validation tests** — oversized inputs, SQL metacharacters, null bytes in username/message fields.
6. **Access control tests** — attempt to read another user's messages with a valid session token (should 403).

Tooling: Wireshark, curl, Burp Suite (Community is free). Document commands and outputs.

---

## File Change Summary

| File | Change |
|---|---|
| `cmake/dependencies.cmake` | Add `find_package(OpenSSL)`, compile def, link OpenSSL to httplib |
| `kds/CMakeLists.txt` | Link `OpenSSL::SSL OpenSSL::Crypto` |
| `libkd/CMakeLists.txt` | Link `OpenSSL::SSL OpenSSL::Crypto` |
| `kds/src/controller/Controller.hh` | `httplib::Server` → `httplib::SSLServer` |
| `kds/src/controller/Controller.cc` | Constructor takes cert/key paths; CSPRNG for tokens |
| `kds/src/main.cc` | Read cert/key from env, pass to Controller |
| `libkd/src/Client.cc` | `httplib::Client` → `httplib::SSLClient` with cert verification |
| `libkd/include/kd/Client.hpp` | Update constructor signature if cert path added |
| `.gitignore` | Add `certs/*.key` |

---

## Order of Work

1. CMake changes (Step 1) — everything else depends on OpenSSL being linked
2. Generate dev cert (Step 2)
3. Server TLS (Step 3) — get `kds` listening on HTTPS
4. Client TLS (Step 4) — get `kdctl` connecting over HTTPS with cert verification
5. CSPRNG tokens (Step 5) — quick win, do alongside Step 3
6. Input validation audit (Step 6) — go through each route handler
7. Pentest and write report (Step 7) — do last, after everything is hardened
