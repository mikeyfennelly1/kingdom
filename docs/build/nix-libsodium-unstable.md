# Nix: libsodium repeatedly re-downloaded (unstable version)

## Error

Running `nix develop` repeatedly downloaded `libsodium-1.0.21-unstable-2026-03-29` even on subsequent runs where it should have been in the Nix store already.

## Cause

`flake.nix` declared three separate nixpkgs inputs, each pinned to a different commit:

```nix
nixpkgs-gcc.url   = "github:NixOS/nixpkgs/01fbdeef...";  # May 2026
nixpkgs-build.url = "github:NixOS/nixpkgs/ee09932c...";  # April 2026
nixpkgs-libs.url  = "github:NixOS/nixpkgs/d233902c...";  # May 2026
```

`libsodium` was sourced from `nixpkgs-libs` at commit `d233902c...` — a very recent nixpkgs-unstable snapshot from May 2026. Two problems compounded:

1. **Unstable package version.** That nixpkgs commit packaged libsodium at `1.0.21-unstable-2026-03-29`, meaning nixpkgs was tracking a pre-release git snapshot of libsodium rather than a tagged release. Pre-release packages often have lower binary cache hit rates on `cache.nixos.org` because hydra hasn't built them for every nixpkgs commit.

2. **Three separate stdenv closures.** Each nixpkgs input resolves its own independent stdenv, glibc, and build toolchain. GCC from `nixpkgs-gcc` and CMake from `nixpkgs-build` link against different glibc store paths — nothing is shared between them. This tripled the effective closure size and tripled the number of cache lookups on every fresh environment.

## Solution

Consolidated to a single nixpkgs input using the `nixpkgs-build` commit (`ee09932c...`), which provides all required packages and has libsodium at the stable `1.0.20` release:

```nix
nixpkgs.url = "github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261";
```

Package versions at this revision:
| Package | Version |
|---|---|
| gcc15 | 15.2.0 |
| cmake | 4.1.2 |
| ninja | 1.13.1 |
| openssl | 3.6.0 |
| libsodium | 1.0.20 (stable) |
| libpqxx | 7.10.1 |

`pkgs.gcc` in this revision defaults to GCC 14, so `pkgs.gcc15` is used explicitly to preserve the GCC 15.2.0 version.

`flake.lock` was updated to remove the two now-unused nixpkgs nodes and point the single `nixpkgs` node at the same rev/narHash.

With a single pinned nixpkgs and all packages at stable releases with good binary cache coverage, `nix develop --no-update-lock-file` will not download or rebuild anything on subsequent runs once the store is populated.
