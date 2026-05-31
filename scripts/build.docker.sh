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

# Extract the nix cache fetch log from the builder stage onto the host.
# --target logs hits the FROM scratch stage; BuildKit reuses the cached builder
# layer so create-closure.sh is not re-executed.
mkdir -p "${PROJ_ROOT}/out"
docker build "${PROJ_ROOT}" \
    --target logs \
    --output "type=local,dest=${PROJ_ROOT}/out"
