# TLS Implementation — Changes Made

## Overview

This document describes the changes made to implement HTTPS/TLS across the Kingdom server and client. All traffic between `kdctl` and `kds` is now encrypted with TLS 1.3. The client verifies the server's certificate before sending any data.

---

## Files Changed

### `devbox.json`

Added `openssl` to the packages list. OpenSSL was already available in the nix store as a transitive dependency of other packages, but was not in the devbox profile, so CMake could not find it.

```json
"openssl",
```

---

### `cmake/dependencies.cmake`

**Problem:** CMake's `find_package(OpenSSL)` searches standard system paths. In a Nix/devbox environment, OpenSSL headers and libraries live in separate nix store paths (the dev package and the runtime package are different store entries). CMake could not find them automatically.

**Fix:** Use pkg-config to locate the correct nix store paths, seed CMake's `OPENSSL_INCLUDE_DIR`, `OPENSSL_SSL_LIBRARY`, and `OPENSSL_CRYPTO_LIBRARY` cache variables with those paths, then call `find_package(OpenSSL 3.0 REQUIRED)`.

This block was added before `FetchContent_MakeAvailable(httplib)`:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_OPENSSL REQUIRED openssl)
find_library(OPENSSL_SSL_LIBRARY NAMES ssl HINTS ${PC_OPENSSL_LIBRARY_DIRS} NO_DEFAULT_PATH)
find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto HINTS ${PC_OPENSSL_LIBRARY_DIRS} NO_DEFAULT_PATH)
set(OPENSSL_INCLUDE_DIR ${PC_OPENSSL_INCLUDE_DIRS} CACHE STRING "" FORCE)
find_package(OpenSSL 3.0 REQUIRED)
```

**Why the order matters:** `FetchContent_MakeAvailable(httplib)` runs cpp-httplib's own CMakeLists.txt, which checks whether the `OpenSSL::SSL` target exists. If it does, httplib automatically sets the `CPPHTTPLIB_OPENSSL_SUPPORT` compile definition on its interface target, making `httplib::SSLServer` and `httplib::SSLClient` available. Without OpenSSL being found first, those types do not exist at compile time.

---

### `kds/CMakeLists.txt`

Added `OpenSSL::SSL` and `OpenSSL::Crypto` to the `kds` link libraries. The server calls OpenSSL APIs directly (via libsodium for CSPRNG, and indirectly via `SSLServer` for TLS).

```cmake
OpenSSL::SSL
OpenSSL::Crypto
```

---

### `libkd/CMakeLists.txt`

Same addition for the shared library. `Client.cc` uses `httplib::Client` with an `https://` URL, which internally uses the OpenSSL symbols.

```cmake
OpenSSL::SSL
OpenSSL::Crypto
```

---

### `kds/src/controller/Controller.hh`

Two changes:

**1. `httplib::Server` → `httplib::SSLServer`**

```cpp
// Before
httplib::Server svr_;

// After
httplib::SSLServer svr_;
```

`SSLServer` inherits from `Server` and has an identical routing API (`Get`, `Post`, `listen`, etc.). The only difference is its constructor requires a certificate and key path, and it wraps every accepted connection in a TLS handshake.

**2. Constructor signature — cert and key paths added**

```cpp
// Before
Controller(std::string host, int port, std::string dbConnectionString, std::string sidecarUrl);

// After
Controller(std::string host, int port, std::string dbConnectionString, std::string sidecarUrl,
           std::string certPath, std::string keyPath);
```

---

### `kds/src/controller/Controller.cc`

**1. Constructor initializer list**

`SSLServer` has no default constructor — it must be given the cert and key paths at construction time. These are passed directly from the constructor parameters:

```cpp
Controller::Controller(std::string host, int port, std::string dbConnectionString,
                       std::string sidecarUrl, std::string certPath, std::string keyPath)
    : host_(std::move(host)), port_(port), sidecarUrl_(std::move(sidecarUrl)),
      svr_(certPath.c_str(), keyPath.c_str()), db_(dbConnectionString) {
```

`SSLServer` loads and validates the certificate and key immediately during construction. If either file is missing or invalid, the constructor throws.

**2. Session token CSPRNG fix**

The old implementation used `std::mt19937_64` seeded from `std::random_device`. `mt19937` is a pseudorandom number generator — its output is deterministic given the seed, and `random_device` on some platforms provides weak entropy. This is a **Broken Authentication** vulnerability (OWASP A07): a predictable session token can be forged.

```cpp
// Before — PRNG, not cryptographically secure
std::random_device rd;
std::mt19937_64 generator(rd());
std::uniform_int_distribution<uint64_t> dist;
std::ostringstream token;
token << std::hex << std::setfill('0') << std::setw(16) << dist(generator)
      << std::setw(16) << dist(generator);
```

```cpp
// After — CSPRNG via libsodium (already a dependency)
unsigned char buf[32];
randombytes_buf(buf, sizeof(buf));
char hex[65];
sodium_bin2hex(hex, sizeof(hex), buf, sizeof(buf));
```

`randombytes_buf` uses the operating system's CSPRNG (`/dev/urandom` on Linux, `arc4random` on macOS). The result is 32 bytes (256 bits) of cryptographically secure randomness, hex-encoded to a 64-character token. The `<random>` and `<iomanip>` includes were removed as they are no longer needed.

---

### `kds/src/controller/configure.cc`

Reads two new required environment variables and passes them to the `Controller` constructor:

```cpp
const char* certPath = std::getenv("KD_TLS_CERT");
const char* keyPath  = std::getenv("KD_TLS_KEY");
if (certPath == nullptr || keyPath == nullptr) {
    throw std::runtime_error("KD_TLS_CERT and KD_TLS_KEY environment variables must be set");
}
return {"0.0.0.0", port, dbUrl, sidecarUrl, certPath, keyPath};
```

The server will refuse to start if either variable is not set. This prevents accidentally running in plaintext mode.

---

### `libkd/include/kd/Client.hpp`

Updated the constructor to accept an optional CA certificate path, and added `caCertPath_` as a private member:

```cpp
// Before
Client(const std::string& baseUrl);

// After
Client(const std::string& baseUrl, std::string caCertPath = "");
```

The default empty string preserves backward compatibility — if no CA cert is provided, the client uses system CAs (correct for production with a proper certificate). For the self-signed dev cert, pass the path to `certs/server.crt`.

---

### `libkd/src/Client.cc`

**1. `makeClient` helper (anonymous namespace)**

All methods previously constructed `httplib::Client cli(baseUrl_)` individually with no TLS configuration. A helper function was added to centralise the client setup:

```cpp
namespace {

httplib::Client makeClient(const std::string& baseUrl, const std::string& caCertPath) {
    httplib::Client cli(baseUrl);
    if (!caCertPath.empty()) {
        cli.set_ca_cert_path(caCertPath);
        cli.enable_server_certificate_verification(true);
    }
    return cli;
}

}  // namespace
```

When `baseUrl` starts with `https://`, `httplib::Client` automatically uses TLS (this is built into cpp-httplib when `CPPHTTPLIB_OPENSSL_SUPPORT` is defined). `set_ca_cert_path` tells OpenSSL which CA to trust for verification. `enable_server_certificate_verification(true)` enables both certificate chain validation and hostname verification.

**2. All methods updated**

Every method that previously did `httplib::Client cli(baseUrl_)` now does:

```cpp
auto cli = makeClient(baseUrl_, caCertPath_);
```

This affects: `getHealth`, `getInfo`, `getConversations`, `signup`, `login`, `logout`, `createConversation`, `sendMessage`, `getMessages`.

---

### `kdctl/src/main.cc`

Added a `--ca-cert` CLI option (also readable from `KD_CA_CERT` env var) and passed it to the `Client` constructor:

```cpp
app.add_option("-c,--ca-cert", caCertPath, "Path to CA certificate for TLS verification")
    ->envname("KD_CA_CERT");

// ...

kd::Client client(serverUrl, caCertPath);
```

---

## Running with TLS

### Generate a development certificate

```bash
mkdir -p certs
openssl req -x509 -newkey rsa:4096 -sha256 -days 365 -nodes \
  -keyout certs/server.key \
  -out certs/server.crt \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

### Start the server

```bash
KD_DB_URL="postgresql://kingdom:kingdom@localhost:5432/kingdom" \
KD_TLS_CERT=certs/server.crt \
KD_TLS_KEY=certs/server.key \
./build/kds/kds
```

### Test with curl

```bash
# Verified HTTPS request — should return {"status":"ok"}
curl --cacert certs/server.crt https://localhost:8080/health

# Unverified — should fail with SSL certificate error
curl https://localhost:8080/health
```

### Use kdctl over HTTPS

```bash
KD_PROTOCOL=https KD_CA_CERT=certs/server.crt ./build/kdctl/kdctl health
```
