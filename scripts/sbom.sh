#!/usr/bin/env bash
# sbom.sh — Generate a Software Bill of Materials for the kds build environment.
#
# Enumerates the full transitive Nix closure of the devShell (every store path
# Nix must materialise — build tools, libs, and all transitive deps), then
# hashes the sorted list so changes between runs are immediately visible.
#
# Usage:
#   nix develop --command bash ./scripts/sbom.sh [output-file]
#
# Defaults:
#   output -> ./out/sbom.md
#
# Must be run inside 'nix develop' — requires $buildInputs
# and $nativeBuildInputs to be set by the Nix devShell environment.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/.."
OUT_DIR="${PROJECT_ROOT}/out"
OUTPUT="${1:-${OUT_DIR}/sbom.md}"

mkdir -p "${OUT_DIR}"

# ─── sanity checks ────────────────────────────────────────────

if ! command -v nix-store &>/dev/null; then
    printf "ERROR: nix-store not found — run inside 'nix develop'\n" >&2
    exit 1
fi

if [[ -z "${buildInputs:-}${nativeBuildInputs:-}" ]]; then
    printf "ERROR: \$buildInputs and \$nativeBuildInputs are empty.\n" >&2
    printf "       This script must run inside 'nix develop' where the devShell env is active.\n" >&2
    printf "       Example: nix develop --command bash ./scripts/sbom.sh\n" >&2
    exit 1
fi

# ─── metadata ─────────────────────────────────────────────────

COMMIT=$(git -C "${PROJECT_ROOT}" rev-parse HEAD 2>/dev/null || echo "unknown")

HOSTNAME_VAL=$(hostname 2>/dev/null || echo "unknown")

IP=""
if command -v ip &>/dev/null; then
    IP=$(ip route get 1.1.1.1 2>/dev/null \
        | awk 'NR==1 { for(i=1;i<=NF;i++) if($i=="src") { print $(i+1); exit } }' \
        || true)
fi
if [[ -z "${IP}" ]] && command -v hostname &>/dev/null; then
    IP=$(hostname -I 2>/dev/null | awk '{print $1}' || true)
fi
IP="${IP:-unknown}"

# ─── resolve full transitive closure ──────────────────────────
# $buildInputs and $nativeBuildInputs are set by the Nix devShell and contain
# the direct dep store paths. nix-store --query --requisites walks the full
# graph to include every transitive dependency at all levels.

printf "Collecting full transitive closure from devShell inputs...\n" >&2

ALL_DEPS=$(
    printf "%s\n" ${buildInputs:-} ${nativeBuildInputs:-} \
        | grep -v '^$' \
        | sort -u \
        | xargs nix-store --query --requisites 2>/dev/null \
        | sort -u
)

if [[ -z "${ALL_DEPS}" ]]; then
    printf "ERROR: nix-store --query --requisites returned no results.\n" >&2
    exit 1
fi

DEP_COUNT=$(printf "%s\n" "${ALL_DEPS}" | wc -l | tr -d ' ')
printf "Found %s packages in full closure.\n" "${DEP_COUNT}" >&2

# ─── hash the dep list ────────────────────────────────────────
# Hash only the sorted store paths, not the surrounding markdown, so that
# hostname/IP differences between machines do not change the hash.

SHA256=$(printf "%s\n" "${ALL_DEPS}" | sha256sum | awk '{print $1}')

# ─── write SBOM ───────────────────────────────────────────────

{
    printf "# SBOM\n\n"
    printf "Commit hash: %s\n" "${COMMIT}"
    printf "Build machine hostname: %s\n" "${HOSTNAME_VAL}"
    printf "Build machine IP address: %s\n" "${IP}"
    printf "\n## All required dependencies:\n\n"
    while IFS= read -r dep; do
        printf "%s\n" "${dep}"
    done <<< "${ALL_DEPS}"
    printf "\n## Dependencies TL;DR\n\n"
    printf "dep count: %s\n" "${DEP_COUNT}"
    printf "sha256: %s\n" "${SHA256}"
} > "${OUTPUT}"

printf "SBOM written to %s\n" "${OUTPUT}" >&2
