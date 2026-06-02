#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

main() {
    cd "${PROJ_ROOT}"

    [[ -f .env ]] || { echo "Error: .env not found — copy .env.example to .env and fill in your values." >&2; exit 1; }
    source .env

    ensure_certs
    start_stack
    wait_for_db
    wait_for_app
    print_status
}

# ─── helpers ────────────────────────────────────────────────

function ensure_certs() {
    if [[ ! -f certs/server.crt || ! -f certs/server.key ]]; then
        echo "==> TLS certs not found — generating..."
        "${SCRIPT_DIR}/init-certs.sh"
    fi
}

function start_stack() {
    echo "==> Starting Kingdom stack..."
    docker compose up -d --build
}

function wait_for_db() {
    echo "==> Waiting for database to be healthy..."
    local tries=0
    until docker compose ps db 2>/dev/null | grep -q "healthy"; do
        tries=$((tries + 1))
        [[ $tries -ge 30 ]] && { echo "Error: database did not become healthy after 60s." >&2; exit 1; }
        sleep 2
    done
    echo "    Database is healthy."
}

function wait_for_app() {
    local port="${KD_PORT:-8080}"
    local ca_cert="certs/ca.crt"
    [[ ! -f "${ca_cert}" ]] && ca_cert="certs/server.crt"

    echo "==> Waiting for kds on port ${port}..."
    local tries=0
    until curl -sf --max-time 3 --cacert "${ca_cert}" "https://localhost:${port}/health" \
            -o /dev/null 2>/dev/null; do
        tries=$((tries + 1))
        [[ $tries -ge 30 ]] && { echo "Error: kds did not respond after 60s." >&2; exit 1; }
        sleep 2
    done
    echo "    kds is up at https://localhost:${port}."
}

function print_status() {
    echo ""
    docker compose ps
    echo ""
    echo "==> Stack is running. Run './scripts/health-check.sh --local --secure' to verify."
}

# ─────────────────────────────────────────────────────────────
main "$@"
