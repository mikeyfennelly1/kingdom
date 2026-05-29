#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

main() {
    teardown
}

function teardown() {
    echo " Stopping Docker Compose services and removing volumes..."
    (cd "${PROJ_ROOT}" && docker compose down -v 2>/dev/null || true)

    echo " Removing kds container if running..."
    docker rm -f kds 2>/dev/null || true

    echo " Freeing port ${KD_PORT}..."
    fuser -k "${KD_PORT}/tcp" 2>/dev/null || true

    echo " Freeing port ${POSTGRES_PORT}..."
    fuser -k "${POSTGRES_PORT}/tcp" 2>/dev/null || true

    return 0
}

# ─── helpers ────────────────────────────────────────────────

# add helper functions here

# ─────────────────────────────────────────────────────────────
main "$@"
