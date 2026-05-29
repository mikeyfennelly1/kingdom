#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

main() {
    # basic script setup
    init_test_vars
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to initialize test variables" >&2
        exit 1
    fi

    export_necessary_vars
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to export necessary environment variables" >&2
        exit 1
    fi

    orient_at_script

    echo "--- Checking connectivity required for build ---"
    check_build_connectivity
    if [[ $? -ne 0 ]]; then
        echo "Error: GitHub is unreachable — Docker build will fail fetching NixOS nixpkgs tarballs" >&2
        echo "NOTE: CHECK IF YOU HAVE A VPN OR FIREWALL CONFIGURED." >&2
        exit 1
    fi

    echo "--- Tearing down test environment ---"
    cleanup
    trap cleanup EXIT # ensures that when script finishes, test objects are cleaned up

    generate_tls_test_cert
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to generate TLS certificate for testing." >&2
        exit 1
    fi

    # application runtime setup
    echo ""
    echo "--- Starting test environment dependencies (docker compose) ---"
    start_services
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to start dependent services for environment." >&2
        exit 1
    fi

    echo "--- waiting for dependent services to be healthy..."
    wait_for_health # times out after given timeframe

    echo "--- Running tests against target on ${KD_BASE_URL}"
    run_tests
    if [[ $? -ne 0 ]]; then
        echo "Error: test run exited with failure code" >&2
        exit 1
    fi
}

function init_test_vars() {
    : "${KD_HOST:=localhost}"
    : "${KD_PORT:=8080}"
    : "${KD_PROTOCOL:=https}"
    : "${KD_WAIT_TIMEOUT:=10}"       # seconds to wait for server health
    
    : "${POSTGRES_USER:=kd_test}"
    : "${POSTGRES_PASSWORD:=kd_test_pass}"
    : "${POSTGRES_DB:=kd_test}"
    : "${POSTGRES_PORT:=5433}"        # host-side port; avoids clashing with a dev DB on 5432
    
    : "${KD_JWT_SECRET:=test-jwt-secret-must-be-at-least-32-characters!!}"
    : "${KD_LOG_LEVEL:=warn}"
    return 0
}

function export_necessary_vars() {
    export POSTGRES_USER POSTGRES_PASSWORD POSTGRES_DB POSTGRES_PORT
    export KD_PORT KD_JWT_SECRET KD_LOG_LEVEL
    
    # The app container reaches Postgres over the docker compose internal network
    export KD_DB_URL="postgresql://${POSTGRES_USER}:${POSTGRES_PASSWORD}@db:5432/${POSTGRES_DB}"
    
    # The test suite (running on the host) reaches the server via the mapped port
    export KD_BASE_URL="${KD_PROTOCOL}://${KD_HOST}:${KD_PORT}"
    return 0
}

function orient_at_script() {
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "${SCRIPT_DIR}"
    return 0
}

function generate_tls_test_cert() {
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
    return 0
}

function check_build_connectivity() {
    local url="https://github.com"
    echo " Verifying connectivity from host -> ${url} (required for NixOS nixpkgs tarballs)..."
    if ! curl -sf --connect-timeout 10 --max-time 10 "${url}" > /dev/null 2>&1; then
        echo "ERROR: Cannot reach ${url}"
        return 1
    fi
    echo " GitHub reachable."
    return 0
}

cleanup() {
  docker compose down -v 2>/dev/null || true
  return 0
}

function start_services() {
    OUTPUT_LOG="services.log"
    echo "starting docker services, forwarding stdout to: ${OUTPUT_LOG}"
    docker compose up -d --build >> "${OUTPUT_LOG}"
    return 0
}

function wait_for_health() {
    echo "Waiting for application stack to start up, by polling server health..."
    echo "Polling server at ${KD_BASE_URL}/health (timeout: ${KD_WAIT_TIMEOUT}s) ---"
    ELAPSED=0
    until curl -sk "${KD_BASE_URL}/health" 2>/dev/null | grep -q '"ok"'; do
      if [ "${ELAPSED}" -ge "${KD_WAIT_TIMEOUT}" ]; then
        echo "ERROR: Server did not become healthy within ${KD_WAIT_TIMEOUT}s"
        docker compose logs
        exit 1
      fi
      sleep 3
      ELAPSED=$((ELAPSED + 3))
      echo "wait time elapsed: ${ELAPSED}/${KD_WAIT_TIMEOUT} seconds."
    done
    echo "--- Server is healthy ---"
    return 0
}

function install_test_dependencies() {
    cd tests
    npm install --prefer-offline --silent
    return 0
}

function run_tests() {
    npm test
    return 0
}

main "$@"
