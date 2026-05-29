#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# test.sh — Kingdom e2e test runner
#
# Starts the server and database in Docker, waits for the health endpoint,
# runs the Vitest e2e suite, then tears everything down.
#
# All defaults can be overridden via environment variables.
# ---------------------------------------------------------------------------

# --- Configurable defaults -------------------------------------------------
: "${KD_HOST:=localhost}"
: "${KD_PORT:=8080}"
: "${KD_PROTOCOL:=https}"
: "${KD_WAIT_TIMEOUT:=120}"       # seconds to wait for server health

: "${POSTGRES_USER:=kd_test}"
: "${POSTGRES_PASSWORD:=kd_test_pass}"
: "${POSTGRES_DB:=kd_test}"
: "${POSTGRES_PORT:=5433}"        # host-side port; avoids clashing with a dev DB on 5432

: "${KD_JWT_SECRET:=test-jwt-secret-must-be-at-least-32-characters!!}"
: "${KD_LOG_LEVEL:=warn}"
# ---------------------------------------------------------------------------

export POSTGRES_USER POSTGRES_PASSWORD POSTGRES_DB POSTGRES_PORT
export KD_PORT KD_JWT_SECRET KD_LOG_LEVEL

# The app container reaches Postgres over the docker compose internal network
export KD_DB_URL="postgresql://${POSTGRES_USER}:${POSTGRES_PASSWORD}@db:5432/${POSTGRES_DB}"

# The test suite (running on the host) reaches the server via the mapped port
export KD_BASE_URL="${KD_PROTOCOL}://${KD_HOST}:${KD_PORT}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# --- TLS certificate -------------------------------------------------------
# Generate a self-signed cert for testing if none is present.
if [ ! -f certs/server.key ]; then
  mkdir -p certs
  openssl req -x509 -newkey rsa:2048 \
    -keyout certs/server.key \
    -out certs/server.crt \
    -days 365 -nodes \
    -subj "/CN=localhost" 2>/dev/null
  echo "Generated self-signed TLS certificate for testing."
fi

export KD_TLS_CERT=certs/server.crt
export KD_TLS_KEY=certs/server.key

# --- Cleanup on exit -------------------------------------------------------
cleanup() {
  echo ""
  echo "--- Tearing down test environment ---"
  docker compose down -v 2>/dev/null || true
}
trap cleanup EXIT

# --- Start services --------------------------------------------------------
echo "--- Starting test environment (docker compose) ---"
docker compose up -d --build

# --- Wait for health -------------------------------------------------------
echo "--- Waiting for server at ${KD_BASE_URL}/health (timeout: ${KD_WAIT_TIMEOUT}s) ---"
ELAPSED=0
until curl -sk "${KD_BASE_URL}/health" 2>/dev/null | grep -q '"ok"'; do
  if [ "${ELAPSED}" -ge "${KD_WAIT_TIMEOUT}" ]; then
    echo "ERROR: Server did not become healthy within ${KD_WAIT_TIMEOUT}s"
    docker compose logs
    exit 1
  fi
  sleep 3
  ELAPSED=$((ELAPSED + 3))
done
echo "--- Server is healthy ---"

# --- Install test dependencies (cached on CI via node_modules layer) -------
echo "--- Installing test dependencies ---"
cd tests
npm install --prefer-offline --silent

# --- Run tests -------------------------------------------------------------
echo "--- Running e2e tests ---"
npm test
