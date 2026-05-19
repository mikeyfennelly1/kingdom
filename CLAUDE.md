# Kingdom — Claude Context

## What This Project Is

A C++20 secure messaging application built for CS4455 Cybersecurity (ISE 2nd Year, 2026).
Submission deadline: **Wednesday 3rd June 2026 at 5:00 PM**.

The project is assessed across four equally weighted minors:
- **Computer Networks & Cybersecurity** (Burkley) — TLS, authentication, security hardening
- **C++ Programming** (Memon) — code structure, OOP, memory management, STL
- **Cryptography** (O'Brien) — E2EE, AEAD, key exchange, password hashing
- **Blockchain** (Le Gear) — tamper-evident message integrity

---

## Project Structure

```
kingdom/
├── libkd/          # Shared library — core logic used by both client and server
│   ├── include/    # Public headers (currently empty)
│   └── src/        # Message.cc, Conversation.cc (currently empty stubs)
├── kdctl/          # Client executable
│   └── src/main.cc
├── kds/            # Server executable (Kingdom Data Server)
│   └── src/main.cc
├── cmake/
│   └── dependencies.cmake   # All third-party deps via FetchContent
├── docs/
│   ├── spec.adoc   # Full project specification and marking rubric
│   └── report.adoc # Communal project report
├── CMakeLists.txt
├── Taskfile.yml
├── devbox.json
└── docker-compose.yml
```

**Module dependency:**
- `libkd` is a shared library; both `kdctl` and `kds` link against it
- Headers from `libkd/include/` are available to both executables
- Source files use `.cc` extension (not `.cpp`)

---

## Build & Dev Commands

Always run inside `devbox shell`.

| Command | What it does |
|---|---|
| `task build` | Configure with CMake + Ninja, then compile. Runs clang-tidy checks. |
| `task run` | Build then run `kds` (the server) |
| `task clean` | Delete `/build` |
| `task uml` | Generate SVG diagram from `docs/arch.puml` |

Manual build if needed:
```bash
cmake -B build -GNinja
cmake --build build
```

Binaries are output to:
- `build/kds/kds`
- `build/kdctl/kdctl`
- `build/libkd/libkd.so` (or `.dylib` on macOS)

---

## Code Style

Enforced by clang-tidy (warnings-as-errors) and clang-format. Violations break the build.

- **Standard:** C++20
- **Formatting:** LLVM style, 4-space indent, 100-char line limit, braces attached (`BreakBeforeBraces: Attach`)
- **Naming:** PascalCase for classes, camelCase or snake_case for methods/variables
- **Includes:** `<...>` for system/library headers, `"..."` for project headers
- **Prefer:** modern C++20 features, smart pointers, STL containers, `const` references
- **Avoid:** raw `new`/`delete`, `malloc`, non-RAII resource management

Run `task format` before committing to auto-fix formatting.

clang-tidy checks enabled: `readability-*`, `bugprone-*`, `modernize-*`, `performance-*`

---

## Third-Party Libraries

All fetched automatically by CMake via `cmake/dependencies.cmake`.

| Library | Use |
|---|---|
| **nlohmann/json** v3.12.0 | JSON serialisation for API payloads |
| **spdlog** v1.13.0 | Logging (use instead of `std::cout` in production code) |
| **cpp-httplib** v0.15.3 | HTTP/HTTPS server (kds) and client (kdctl). Requires OpenSSL. |
| **CLI11** v2.4.2 | CLI argument parsing for kdctl |
| **OpenSSL** 3.0+ | TLS, AES, key derivation, hashing, CSPRNG |
| **GoogleTest** v1.14.0 | Unit testing |

To use a library in a target, add it to `target_link_libraries` in the relevant `CMakeLists.txt`.

---

## Infrastructure

- **PostgreSQL 16** via Docker Compose — run with `docker compose up -d`
- **Devbox** — reproducible dev environment, run `devbox shell` to enter it
- **PlantUML** — architecture diagrams, source goes in `docs/arch.puml`

---

## Current State

The project is a skeleton. CMake, tooling, and dependencies are all wired up and working. Source files are stubs — don't worry about their content for now.
