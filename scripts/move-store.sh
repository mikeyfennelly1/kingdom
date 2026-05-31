#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

main() {
    mv /app/unpacked-store/nix/store /nix/store
}

# ─── helpers ────────────────────────────────────────────────

# add helper functions here

# ─────────────────────────────────────────────────────────────
main "$@"
