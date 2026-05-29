#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/.env"

if [[ ! -f "${ENV_FILE}" ]]; then
    echo "Error: ${ENV_FILE} not found — copy .env.example to .env and fill in your values." >&2
    exit 1
fi

# Load .env, expanding variable references and ignoring comments/blanks
set -a
# shellcheck source=/dev/null
source "${ENV_FILE}"
set +a

docker build "${SCRIPT_DIR}" \
    --build-arg POSTGRES_USER="${POSTGRES_USER}" \
    --build-arg POSTGRES_PASSWORD="${POSTGRES_PASSWORD}" \
    --build-arg POSTGRES_DB="${POSTGRES_DB}" \
    --build-arg POSTGRES_HOST="${POSTGRES_HOST:-db}" \
    --build-arg POSTGRES_PORT="${POSTGRES_PORT}" \
    --build-arg KD_TLS_CERT="${KD_TLS_CERT}" \
    --build-arg KD_TLS_KEY="${KD_TLS_KEY}" \
    --build-arg KD_JWT_TTL_SECONDS="${KD_JWT_TTL_SECONDS}" \
    --build-arg KD_LOG_LEVEL="${KD_LOG_LEVEL:-info}" \
    --build-arg KD_PORT="${KD_PORT:-8080}" \
    --tag kds:latest \
    "$@"
