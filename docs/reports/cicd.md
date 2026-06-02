# CI/CD and Build Infrastructure — Kingdom

CS4455 Cybersecurity | Computer Networks & Cybersecurity (Mark Burkley) | 2026

---

## 1. Overview

Kingdom's CI/CD pipeline is built around two ideas: Nix as a single, pinned source of truth for every build dependency, and Docker as the deployment and release unit. Together they mean every build — on a developer's laptop, in GitHub Actions, or in the final Docker image — is guaranteed to use exactly the same compiler, libraries, and tools.

---

## 2. Nix — Reproducible Dependency Management

### 2.1 The Nix Flake

The project's root `flake.nix` declares every tool and library the build requires. The entire nixpkgs input is pinned to a single commit:

```nix
nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
# Pinned to ee09932cedcef15aaf476f9343d1dea2cb77e261
```

A corresponding `flake.lock` records the exact resolved revision so that `nix flake update` is an explicit, deliberate act rather than an implicit one that silently changes what gets installed.

### 2.2 Version Assertions

Every package in the flake carries a hard assertion on its exact version:

```nix
assert pkgs.gcc15.version == "15.2.0";
assert pkgs.openssl.version == "3.6.0";
assert pkgs.libsodium.version == "1.0.20";
assert pkgs.cmake.version == "4.1.2";
# ... and so on for all 20+ packages
```

If the nixpkgs pin is ever updated and any package version shifts, the flake evaluation fails immediately with a clear error. This turns a silent library version drift — the kind that produces "works on my machine" bugs — into a loud, explicit breakage that must be resolved consciously before the build can proceed.

### 2.3 Two Shells — Full and Slim

The flake defines two development shells:

| Shell | `nix develop` command | Purpose |
|-------|----------------------|---------|
| `devShells.default` | `nix develop` | Full local dev shell — includes Qt6 for building the `kdctl` GUI client |
| `devShells.kds` | `nix develop .#kds` | Slim shell for CI and Docker — server only, no Qt6 |

The slim `.#kds` shell avoids materialising the Qt6 dependency tree (~200 GUI-layer packages: X11, Wayland, GTK, harfbuzz, ICU, Vulkan, fontconfig) that the headless server binary never uses. All GitHub Actions workflows and the Docker build use `.#kds` exclusively.

---

## 3. CMake — Only Nix Packages Allowed

The `CMakeLists.txt` explicitly disables every path through which CMake might reach the host system's installed libraries:

```cmake
# HARD RULE: never search system paths
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)

# HARD RULE: only search explicitly provided prefixes
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

When CMake runs inside the Nix shell, the environment variables `PKG_CONFIG_PATH`, `CMAKE_PREFIX_PATH`, and `LD_LIBRARY_PATH` are populated automatically by Nix with `/nix/store/...` paths. With system paths blocked and root-path mode set to `ONLY`, the build can only resolve dependencies from those Nix-provided paths. If a library is not in the flake, `find_package` fails and the build stops — there is no silent fallback to whatever happens to be installed on the host OS.

The install prefix is also redirected into the build tree:

```cmake
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "" FORCE)
```

This ensures that GNUInstallDirs never seeds `/usr/local` paths into target properties, keeping the build fully self-contained.

---

## 4. Docker — Platform Independence, Clean Releases, Rollbacks

### 4.1 The Nix Closure in Docker

Because `kds` links against libraries in `/nix/store`, shipping just the binary is not enough — the container needs those store paths too. `scripts/create-closure.sh` handles this:

1. Builds the `kds` binary inside the `.#kds` Nix shell.
2. Runs `ldd` on the binary to find its direct `/nix/store/...` dependencies.
3. Calls `nix-store --query --requisites` to compute the full transitive closure (every library the libraries depend on, recursively).
4. Tars the entire closure into `out/kds-closure.tar.gz` preserving absolute paths.

The `Dockerfile` is then minimal:

```dockerfile
FROM alpine:3.20
COPY out/kds-closure.tar.gz /tmp/
RUN apk add --no-cache tar && tar -xzPf /tmp/kds-closure.tar.gz && rm /tmp/kds-closure.tar.gz
COPY out/kds /app/kds
ENTRYPOINT ["/app/kds"]
```

The closure is unpacked at the same `/nix/store/...` paths the binary was linked against, so it finds its libraries exactly as if running inside the Nix shell. The base image (`alpine:3.20`) provides the container OS, but the entire C++ runtime and all dependencies come from the Nix closure — not from Alpine's package manager. The image is therefore fully reproducible and independent of whatever Linux distribution or package versions happen to be on the deployment host.

### 4.2 Platform Independence

The Docker image is the deployment unit. Whether `kds` is running on the project's Ubuntu VM, a fresh Debian machine, or a cloud instance, it always runs with exactly the same binaries and libraries. There is no requirement for the host to have OpenSSL, libsodium, libpqxx, or any other library installed — they are all inside the container closure.

The Docker Compose file (`docker-compose.yml`) manages the full server-side stack:

```
docker-compose.yml
├── app (kds)          — built from Docker Hub; Nix closure bundled inside
├── sidecar            — Node.js blockchain bridge (node:lts-alpine)
└── db                 — PostgreSQL 16 (postgres:16)
```

`kds` does not start until PostgreSQL reports healthy via the built-in `pg_isready` health check, preventing startup failures due to race conditions between containers.

### 4.3 Releasing

Releases are driven by git tags. The `version` workflow fires on every push to `main` and automatically bumps the patch version and pushes a semver tag (`v1.2.3`) using `mathieudutour/github-tag-action`. This triggers the `docker-release` workflow, which:

1. Installs Nix using the Determinate Systems action.
2. Runs `scripts/create-closure.sh` to build the binary and assemble the closure inside the pinned Nix environment.
3. Logs into Docker Hub and pushes the image with two tags: the exact semver (`mikeyfennelly/kds:1.2.3`) and the floating `latest`.

The `release` workflow fires on version tags pushed directly and additionally publishes the image to the GitHub Container Registry (`ghcr.io`) and creates a GitHub release with auto-generated release notes.

Every Docker Hub tag is therefore an immutable, independently addressable build tied to a specific commit and a specific set of pinned Nix packages.

### 4.4 Rolling Back

Because every release produces a tagged Docker image, rolling back is a one-line change in the Compose file or the Ansible deploy playbook — change the image tag to any previous version and restart the stack:

```yaml
# docker-compose.yml
app:
  image: mikeyfennelly/kds:1.4.2   # ← change to any prior semver
```

The old image is already on Docker Hub; no rebuild is required. The deployment Ansible playbook (`ansible/deploy.yml`) handles stopping the running containers, pulling the specified image, and restarting the stack.

### 4.5 Keeping the Host Clean

Running `kds` and PostgreSQL as containers means the deployment VM does not need a C++ runtime, database server, or any application-level dependency installed directly on the host OS. The host only needs Docker. There is no risk of library version conflicts between Kingdom and other software on the same machine, and tearing down the entire stack leaves nothing behind — `docker compose down -v` removes the containers, and the host filesystem is back to a clean state.

---

## 5. GitHub Actions Workflows

| Workflow | Trigger | What it does |
|----------|---------|-------------|
| `build` | PR → `main` | Installs Nix, runs `nix develop .#kds --command bash ./scripts/build.sh`. Build failure blocks merge. |
| `tests` | Push to `main` (source paths only) | Spins up Docker Buildx and PostgreSQL, runs the end-to-end test suite via `scripts/test.sh`. |
| `version` | Push to `main` | Auto-bumps patch version, pushes a semver git tag. |
| `docker-release` | After `version` succeeds | Builds binary + Nix closure, pushes Docker image to Docker Hub with semver and `latest` tags. |
| `release` | Push of `v*.*.*` tag | Full CI (build + e2e tests) then publishes to GitHub Container Registry and creates a GitHub release. |
| `commit-lint` | PR | Enforces conventional commit format on all PR commits. |

The `build` and `tests` workflows both install Nix via `DeterminateSystems/nix-installer-action` and configure the `magic-nix-cache-action`, which caches Nix store paths between CI runs. This means subsequent builds only fetch store paths that have actually changed, keeping CI run times short despite the large closure.

---

## 6. Local Development Flow

```
developer machine
└── nix develop            → devShells.default (full shell, Qt6 included)
    └── task build         → cmake -B build -GNinja + cmake --build build
        └── CMakeLists.txt → blocked from touching host system paths
            └── finds all deps via Nix-provided PKG_CONFIG_PATH / CMAKE_PREFIX_PATH
```

The build script (`scripts/build.sh`) verifies connectivity to `github.com` before starting, since the Nix flake fetches source archives for vendored dependencies (`nlohmann/json`, `spdlog`, `cpp-httplib`, `CLI11`, `GoogleTest`) via `FetchContent`. Once cached locally, subsequent builds are fully offline.

Running `task build` inside the Nix shell produces binaries whose only external dependencies are `/nix/store/...` paths — not system libraries. The same binary can be bundled into a Docker image via `create-closure.sh` without any changes.
