#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/.env"

if [[ ! -f "${ENV_FILE}" ]]; then
    echo "Error: ${ENV_FILE} not found — copy .env.example to .env and fill in your values." >&2
    exit 1
fi

set -a
# shellcheck source=/dev/null
source "${ENV_FILE}"
set +a

docker run \
    -e POSTGRES_USER="${POSTGRES_USER}" \
    -e POSTGRES_PASSWORD="${POSTGRES_PASSWORD}" \
    -e POSTGRES_DB="${POSTGRES_DB}" \
    -e POSTGRES_HOST="${POSTGRES_HOST:-db}" \
    -e POSTGRES_PORT="${POSTGRES_PORT}" \
    -e KD_DB_URL="${KD_DB_URL}" \
    -e KD_TLS_CERT="${KD_TLS_CERT}" \
    -e KD_TLS_KEY="${KD_TLS_KEY}" \
    -e KD_JWT_SECRET="${KD_JWT_SECRET}" \
    -e KD_JWT_TTL_SECONDS="${KD_JWT_TTL_SECONDS}" \
    -e KD_LOG_LEVEL="${KD_LOG_LEVEL:-info}" \
    -e KD_PORT="${KD_PORT:-8080}" \
    -p "${KD_PORT:-8080}:${KD_PORT:-8080}" \
    kds:latest \
    "$@"
