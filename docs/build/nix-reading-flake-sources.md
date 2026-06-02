# Reading flake.nix and flake.lock: tracing sources and store paths

This document explains how to read the two flake files to answer two questions
without downloading anything:

1. **Where on the internet does each dependency come from?**
2. **Where on this machine will it land?**

All examples use the actual files in this repository.

---

## The two files and their roles

| File | Purpose |
|---|---|
| `flake.nix` | Declares *what* you want (inputs by name, packages from those inputs) |
| `flake.lock` | Records *exactly* where each input was resolved to (commit, hash, timestamp) |

`flake.nix` is the human-authored intent. `flake.lock` is the machine-generated
pin. When Nix evaluates the flake it reads the lock file, not the URL in
`flake.nix`, so the lock file is the authoritative record of what will actually
be fetched.

---

## Part 1 — Where on the internet does it come from?

### Step 1: find the input in flake.nix

```nix
# flake.nix
inputs = {
  nixpkgs.url = "github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261";
  flake-utils.url = "github:numtide/flake-utils";
};
```

The `url` field uses a Nix flake URL scheme. The `github:` scheme is the most
common. Its structure is:

```
github:<owner>/<repo>[/<rev-or-branch>]
```

### Step 2: find the locked node in flake.lock

Every input declared in `flake.nix` has a corresponding node in `flake.lock`.
Each node has two sections:

- `original` — what the URL in `flake.nix` resolved from (before locking)
- `locked` — the exact pinned state Nix will use

```json
"nixpkgs": {
  "locked": {
    "lastModified": 1763934636,
    "narHash": "sha256-9glbI7f1uU+yzQCq5LwLgdZqx6svOhZWkd4JRY265fc=",
    "owner": "NixOS",
    "repo": "nixpkgs",
    "rev": "ee09932cedcef15aaf476f9343d1dea2cb77e261",
    "type": "github"
  }
}
```

### Step 3: construct the download URL

For `type: "github"`, Nix fetches a tarball from GitHub's archive endpoint:

```
https://github.com/<owner>/<repo>/archive/<rev>.tar.gz
```

Substituting the locked values:

```
https://github.com/NixOS/nixpkgs/archive/ee09932cedcef15aaf476f9343d1dea2cb77e261.tar.gz
```

You can paste that URL directly into a browser or `curl` to fetch the exact
source tree Nix will use.

For `flake-utils`:
```
https://github.com/numtide/flake-utils/archive/11707dc2f618dd54ca8739b309ec4fc024de578b.tar.gz
```

### Step 4: verify integrity with narHash

The `narHash` field is a SHA-256 hash of the downloaded tree after it has been
serialised as a **NAR** (Nix ARchive — Nix's deterministic archive format,
analogous to tar but with a fixed byte order for all metadata).

```
narHash: "sha256-9glbI7f1uU+yzQCq5LwLgdZqx6svOhZWkd4JRY265fc="
```

The value is base64-encoded SHA-256. When Nix fetches the tarball it unpacks
it, re-serialises it as a NAR, hashes that NAR, and compares against this
value. If they differ, the fetch is rejected. This means `narHash` + `rev`
together uniquely and verifiably identify a snapshot of the repository.

You can verify manually:
```bash
nix hash file --type sha256 --base64 /path/to/unpacked-tree
```

### URL schemes reference

| Scheme | Example | Download URL pattern |
|---|---|---|
| `github:` | `github:NixOS/nixpkgs/abc123` | `https://github.com/<owner>/<repo>/archive/<rev>.tar.gz` |
| `gitlab:` | `gitlab:owner/repo/abc123` | `https://gitlab.com/<owner>/<repo>/-/archive/<rev>/<repo>-<rev>.tar.gz` |
| `tarball:` | `tarball:https://example.com/foo.tar.gz` | the URL directly |
| `git+https:` | `git+https://github.com/foo/bar` | cloned via git, `rev` is the commit |
| `path:` | `path:/some/local/path` | local filesystem, no download |

---

## Part 2 — Where on this machine will it land?

Nix stores everything under `/nix/store/`. Each path looks like:

```
/nix/store/<hash>-<name>-<version>
```

The `<hash>` is a 32-character base32 string derived deterministically from
the full description of how that output was produced (its **derivation**). Two
identical builds of the same package produce the same hash; any change to
inputs, flags, or source produces a different hash.

There are two distinct store paths to know about:

### The source store path (the downloaded input)

When Nix fetches a flake input it stores the unpacked source tree at:

```
/nix/store/<hash>-source
```

The hash here is computed directly from the `narHash`. It is a fixed-output
derivation — Nix trusts the narHash and does not need to know how the source
was produced, only that its NAR hash matches.

### The built package store path

When Nix builds a package from that source (e.g. compiling libsodium), the
result lands at a path whose hash is derived from the entire derivation: the
source hash, all build dependencies, all compiler flags, the build script, etc.

For `libsodium` from this flake:
```
/nix/store/3d0pz2rax1v3b05lwjqlm4zi6dklnhs5-libsodium-1.0.20
```

For `gcc15`:
```
/nix/store/c0gzhvsgxrl8lwdy1q9qwyjzandqjw53-gcc-wrapper-15.2.0
```

### Computing store paths without downloading

You do not need to download or build anything to learn a store path. Nix can
evaluate the derivation graph and report the paths it would produce:

**Output path of a package:**
```bash
nix eval --raw 'github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261#libsodium.outPath'
# /nix/store/3d0pz2rax1v3b05lwjqlm4zi6dklnhs5-libsodium-1.0.20
```

**The .drv file (the derivation itself — the build recipe):**
```bash
nix path-info --derivation 'github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261#libsodium'
# /nix/store/ja4yw14bdx47k2dvmwpk445m279kyv51-libsodium-1.0.20.drv
```

**Read the full derivation (all inputs, flags, build script):**
```bash
nix show-derivation 'github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261#libsodium'
```

**Check whether a path is already in the local store:**
```bash
nix path-info /nix/store/3d0pz2rax1v3b05lwjqlm4zi6dklnhs5-libsodium-1.0.20
# prints the path if present, errors if not
```

**Check whether a pre-built binary exists in the remote cache (without downloading it):**
```bash
nix path-info --store https://cache.nixos.org /nix/store/3d0pz2rax1v3b05lwjqlm4zi6dklnhs5-libsodium-1.0.20
# prints the path if the binary cache has it, errors if not
```

**Query all outputs of the dev shell in this project:**
```bash
nix eval --raw '.#devShells.x86_64-linux.default.outPath'
```

---

## Putting it all together: full trace for libsodium

```
flake.nix
  └─ nixpkgs.url = "github:NixOS/nixpkgs/ee09932c..."

flake.lock  →  nodes.nixpkgs.locked
  ├─ type:    "github"
  ├─ owner:   "NixOS"
  ├─ repo:    "nixpkgs"
  ├─ rev:     "ee09932cedcef15aaf476f9343d1dea2cb77e261"
  └─ narHash: "sha256-9glbI7f1uU+yzQCq5LwLgdZqx6svOhZWkd4JRY265fc="

Download URL (reconstructed):
  https://github.com/NixOS/nixpkgs/archive/ee09932cedcef15aaf476f9343d1dea2cb77e261.tar.gz

nixpkgs at that rev defines libsodium as a derivation referencing
  upstream source: https://download.libsodium.org/libsodium/releases/libsodium-1.0.20.tar.gz
  (visible in: nix show-derivation ...#libsodium)

Built output lands at:
  /nix/store/3d0pz2rax1v3b05lwjqlm4zi6dklnhs5-libsodium-1.0.20
```

---

## Quick-reference commands

```bash
# Reconstruct download URL from lock entry
# github: → https://github.com/<owner>/<repo>/archive/<rev>.tar.gz

# Compute expected store path without downloading
nix eval --raw '<flakeref>#<attr>.outPath'

# Show the .drv (build recipe)
nix path-info --derivation '<flakeref>#<attr>'

# Read the full derivation
nix show-derivation '<flakeref>#<attr>'

# Check if already in local store
nix path-info <store-path>

# Check if pre-built binary is in remote cache
nix path-info --store https://cache.nixos.org <store-path>

# Show all locked inputs for this flake
nix flake metadata .

# Show locked inputs as JSON
nix flake metadata . --json | jq '.locks.nodes'
```
