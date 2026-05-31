#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/../"
ENV_FILE="${PROJ_ROOT}/.env"

if [[ ! -f "${ENV_FILE}" ]]; then
    printf "ERROR: ${ENV_FILE} not found — copy .env.example to .env and fill in your values." >&2
    exit 1
fi

# Load .env, expanding variable references and ignoring comments/blanks
set -a
# shellcheck source=/dev/null
source "${ENV_FILE}"
set +a

# ── Step 1: Build binary and closure ──────────────────────────────────────────
# Mount the host /nix into the builder container so nix develop finds all
# packages already present — no downloads from cache.nixos.org.
# Artifacts land in the project tree via the /app bind mount:
#   out/kds-closure.tar.gz   — runtime Nix closure
#   out/nix-cache-fetches.log — fetches that hit cache.nixos.org (should be empty)
#   build/kds/kds            — compiled binary
printf "Building kds with host Nix store mounted...\n"
docker run --rm \
    -v /nix:/nix \
    -v "${PROJ_ROOT}:/app" \
    -w /app \
    nixos/nix:2.34.7 \
    bash -c 'bash ./scripts/configure-nix-host.sh && bash ./scripts/create-closure.sh'

# Copy binary into out/ — build/ is excluded from the Docker build context
# (.dockerignore), so we stage the binary alongside the closure tarball.
mkdir -p "${PROJ_ROOT}/out"
cp "${PROJ_ROOT}/build/kds/kds" "${PROJ_ROOT}/out/kds"

# ── Step 2: Package into minimal runtime image ────────────────────────────────
printf "Packaging runtime Docker image...\n"
docker build "${PROJ_ROOT}" \
    --build-arg POSTGRES_USER="${POSTGRES_USER}" \
    --build-arg POSTGRES_DB="${POSTGRES_DB}" \
    --build-arg POSTGRES_HOST="${POSTGRES_HOST:-db}" \
    --build-arg POSTGRES_PORT="${POSTGRES_PORT}" \
    --build-arg KD_TLS_CERT="${KD_TLS_CERT}" \
    --build-arg KD_JWT_TTL_SECONDS="${KD_JWT_TTL_SECONDS}" \
    --build-arg KD_LOG_LEVEL="${KD_LOG_LEVEL:-info}" \
    --build-arg KD_PORT="${KD_PORT:-8080}" \
    --tag kds:latest \
    "$@"
