# Nix Store: Management, Dependency Graphs, and Download Resolution

## What the Nix Store Is

The Nix store lives at `/nix/store/` and is the single source of truth for every package, library, and derivation on the host. Each entry is a directory named by a cryptographic hash of its build inputs:

```
/nix/store/<hash>-<name>-<version>/
```

The hash covers the derivation (build recipe), all dependencies, build flags, and environment variables. Two packages built with even a single differing input get different hashes and coexist without conflict. Nothing in `/nix/store/` is ever mutated after it is written.

## How Nix Manages the Store

### Realising a derivation

When you run `nix-build`, `nix-env -i`, or `nix-shell`, Nix:

1. **Evaluates** the Nix expression to produce a `.drv` file (a build plan) in `/nix/store/`.
2. **Checks** whether the output path (the hash-named directory) already exists in the store. If yes, nothing happens.
3. **Fetches** any missing source archives from substituters (binary caches such as `cache.nixos.org`).
4. **Builds** the derivation inside an isolated sandbox if no cached binary exists.
5. **Registers** the output as a GC root or links it into a profile.

Paths are immutable once created. Nix never overwrites or patches an existing store path.

### Garbage collection

The store accumulates paths over time. Nix tracks *GC roots*: paths in `/nix/var/nix/gcroots/` that are actively in use. Everything reachable from a GC root is live; everything else can be deleted.

```bash
# Show what is currently a GC root
nix-store --gc --print-roots

# Delete unreachable store paths (dry run first)
nix-store --gc --print-dead
nix-store --gc
```

Profiles (`~/.nix-profile`, `/nix/var/nix/profiles/`) are symlinks that are themselves GC roots, so their full closure is always kept.

---

## Analysing the Dependency Graph

### Querying direct and transitive dependencies

```bash
# Direct runtime dependencies of a store path
nix-store --query --references /nix/store/<hash>-foo

# Full transitive closure (everything it could ever need at runtime)
nix-store --query --requisites /nix/store/<hash>-foo

# Build-time dependencies only
nix-store --query --referrers /nix/store/<hash>-foo

# Everything that depends *on* this path (reverse closure)
nix-store --query --referrers-closure /nix/store/<hash>-foo
```

### Visualising the graph

```bash
# Emit a Graphviz .dot file for the closure
nix-store --query --graph /nix/store/<hash>-foo | dot -Tsvg > dep-graph.svg

# Condensed tree view (shows only unique paths at each level)
nix-store --query --tree /nix/store/<hash>-foo
```

### Finding why a dependency is included

```bash
# Which paths in the closure reference libssl?
nix-store --query --requisites /nix/store/<hash>-foo \
  | xargs -I{} nix-store --query --references {} \
  | grep openssl
```

### Using nix path-info (flakes / modern Nix)

```bash
# Closure size on disk
nix path-info --recursive --size /nix/store/<hash>-foo \
  | awk '{sum+=$2} END {print sum/1e6 " MB"}'

# Human-readable closure with sizes
nix path-info -rsSh /nix/store/<hash>-foo
```

---

## How Nix Decides What to Download

### Substitution

Before building from source Nix queries *substituters* (binary caches) in priority order. The default is `https://cache.nixos.org`. For each missing output path:

1. Nix computes the expected output hash from the `.drv`.
2. It asks each substituter in turn for a NAR (Nix ARchive) at that hash.
3. If a signed NAR is found, Nix downloads and unpacks it — this is called *substitution*.
4. If no substituter has the path, Nix builds it locally.

### Checking cache status without downloading

```bash
# Does cache.nixos.org have this path?
nix-store --query --valid /nix/store/<hash>-foo 2>&1

# Check a specific substituter
nix path-info --store https://cache.nixos.org /nix/store/<hash>-foo
```

### Inspecting what would be fetched for a build

```bash
# Dry run: show paths that would be downloaded vs built
nix-build --dry-run /path/to/default.nix

# Same for a shell
nix-shell --dry-run shell.nix
```

The output distinguishes:
- `these paths will be fetched` — present on a substituter
- `these derivations will be built` — must be built locally

### Evaluating the full download footprint for a nix-shell

```bash
nix-shell --dry-run shell.nix 2>&1 \
  | grep "will be fetched" -A9999 \
  | awk '{print $1}' \
  | xargs nix path-info --size 2>/dev/null \
  | awk '{sum+=$2} END {print "Total NAR bytes:", sum}'
```

---

## Useful One-Liners

| Goal | Command |
|---|---|
| Size of a store path on disk | `du -sh /nix/store/<hash>-foo` |
| Size of full closure | `nix path-info -rsSh /nix/store/<hash>-foo \| tail -1` |
| All shared libraries in a closure | `nix-store --query --requisites /nix/store/<hash>-foo \| xargs -I{} find {} -name '*.so*' 2>/dev/null` |
| Export a closure to a tarball | `nix-store --export $(nix-store -qR /nix/store/<hash>-foo) > closure.nar` |
| Import a closure from a tarball | `nix-store --import < closure.nar` |
| List all installed store paths | `nix-store --query --requisites ~/.nix-profile` |
