# Docker Image Closure Size

The `kds` runtime image is built in two stages: a Nix builder that compiles the binary, then a minimal Alpine runtime that receives the binary and its Nix closure. The closure is collected by walking `ldd` output through `nix-store --query --requisites`, which pulls in the full transitive dependency graph.

## What's in the closure

As of `kds-closure.tar.gz` on `build/docker-img-weight-fixes`:

| Package | Why it's there |
|---|---|
| `glibc-2.42` | C standard library — direct runtime dep |
| `openssl-3.6.1` | TLS and crypto — direct dep via cpp-httplib |
| `libpq-18.2` | PostgreSQL client — direct dep |
| `libpqxx-7.10.5` | C++ PostgreSQL wrapper — direct dep |
| `libsodium-1.0.21` | Crypto primitives — direct dep |
| `gcc-15/14-lib` | libstdc++ and GCC runtime — direct dep |
| `curl-8.19.0` | HTTP client — **transitive dep of libpq**, not used by kds directly |
| `nghttp2/nghttp3/ngtcp2` | HTTP/2 and HTTP/3 — transitive via curl |
| `brotli`, `zlib`, `zstd` | Compression — transitive via curl |
| `krb5-1.22.1` | Kerberos — transitive via libpq |
| `libssh2-1.11.1` | SSH — transitive via curl |
| `libidn2`, `libpsl`, `publicsuffix-list` | Unicode/IDN — transitive via curl |
| `keyutils-1.6.3` | Kernel keys — transitive via krb5 |

The single largest contributor is **glibc's `gconv/` subdirectory** — ~200 character encoding conversion modules that kds will never load at runtime.

## Options for reducing size

### Option 1: Strip useless files from the tarball (easy, safe)

Exclude known-unnecessary subtrees when creating the closure tarball in the Dockerfile:

```dockerfile
RUN ldd /app/build/kds/kds \
      | awk '$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }' \
      | sort -u \
      | xargs nix-store --query --requisites \
      | sort -u \
      | tar -czPf /tmp/kds-closure.tar.gz \
          --exclude='*/gconv/*' \
          --exclude='*/share/man/*' \
          --exclude='*/share/doc/*' \
          --exclude='*/share/examples/*' \
          --exclude='*/share/et/*' \
          --exclude='*/lib/libasan*' \
          --exclude='*/lib/libtsan*' \
          --exclude='*/lib/libubsan*' \
          --exclude='*/lib/libhwasan*' \
          --exclude='*/lib/liblsan*' \
          --files-from=-
```

Expected savings: ~40–50% reduction. The gconv exclusion alone removes the bulk of glibc's footprint. The sanitizer lib exclusions (libasan, libtsan, libubsan, etc.) are safe because Release builds don't load them.

### Option 2: Direct deps only (moderate risk)

Replace `--query --requisites` (full transitive closure) with `--query --references` (direct deps only). This cuts out curl and its entire transitive tree since libpq only references curl at configure time, not at runtime.

```bash
xargs nix-store --query --references
```

Risk: if any dep genuinely needs a transitive package at runtime (via dlopen, for example), the binary will crash on startup. Should be validated with a test run before shipping.

### Option 3: Statically linked binary with musl (best long-term)

Change `build.shell.nix` to build against `pkgsStatic` (musl-based). The resulting binary has no shared library dependencies, so the entire closure collection step in the Dockerfile becomes unnecessary. Stage 2 reduces to:

```dockerfile
FROM scratch
COPY --from=builder /app/build/kds/kds /kds
ENTRYPOINT ["/kds"]
```

This produces the smallest possible image. It requires:
- Updating `build.shell.nix` to use `pkgs.pkgsStatic.*` for all deps
- Ensuring CMakeLists passes `-static` linker flags
- Verifying OpenSSL and libpq static archives are available in the Nix derivations

Trade-off: more complex build configuration; some libraries (e.g. libpq with GSSAPI) can be tricky to link statically.
