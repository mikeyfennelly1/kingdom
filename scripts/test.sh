#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/.."

main() {
    # parse flags
    REBUILD=false
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --rebuild|-r)
                REBUILD=true
                shift
                ;;
            *)
                echo "Unknown option: $1" >&2
                echo "Usage: $0 [--rebuild|-r]" >&2
                exit 1
                ;;
        esac
    done

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

    pushd "${PROJECT_ROOT}"
     trap 'popd > /dev/null' EXIT INT TERM ERR

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
    if [[ "${REBUILD}" == "true" ]]; then
        echo ""
        echo "--- Building docker artifacts ---"
        build_services
        if [[ $? -ne 0 ]]; then
            echo "Error: failed to build dependent services for environment." >&2
            exit 1
        fi
    else
        echo ""
        echo "--- Skipping build (pass --rebuild to force a fresh build) ---"
    fi

    echo "--- Starting test environment dependencies (docker compose) ---"
    start_services
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to start dependent services for environment." >&2
        exit 1
    fi

    echo "--- waiting for dependent services to be healthy..."
    wait_for_health # times out after given timeframe

    echo "--- Installing test dependencies ---"
    install_test_dependencies

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
    : "${KD_WAIT_TIMEOUT:=120}"      # seconds to wait for server health
    
    : "${POSTGRES_USER:=kd_test}"
    : "${POSTGRES_PASSWORD:=kd_test_pass}"
    : "${POSTGRES_DB:=kd_test}"
    : "${POSTGRES_PORT:=5433}"        # host-side port; avoids clashing with a dev DB on 5432
    
    : "${KD_JWT_SECRET:=test-jwt-secret-must-be-at-least-32-characters!!}"
    : "${KD_JWT_TTL_SECONDS:=3600}"
    : "${KD_LOG_LEVEL:=warn}"
    return 0
}

function export_necessary_vars() {
    export POSTGRES_USER POSTGRES_PASSWORD POSTGRES_DB POSTGRES_PORT
    export KD_PORT KD_JWT_SECRET KD_JWT_TTL_SECONDS KD_LOG_LEVEL
    
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
    local cert_dir="${SCRIPT_DIR}/../certs"
    if [ ! -f "${cert_dir}/server.key" ]; then
      mkdir -p "${cert_dir}"
      openssl req -x509 -newkey rsa:2048 \
        -keyout "${cert_dir}/server.key" \
        -out "${cert_dir}/server.crt" \
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

function build_services() {
    OUTPUT_LOG="buildtime.test.log"
    echo "removing file if exists: ${OUTPUT_LOG}"
    rm "${OUTPUT_LOG}" || true
    echo "building docker services, forwarding to: ${OUTPUT_LOG}"
    docker compose build >> "${OUTPUT_LOG}" 2>&1
    return 0
}

function start_services() {
    OUTPUT_LOG="runtime.test.log"
    echo "removing file if exists: ${OUTPUT_LOG}"
    rm "${OUTPUT_LOG}" || true
    echo "starting docker services, stdout to: ${OUTPUT_LOG}"
    docker compose up -d >> "${OUTPUT_LOG}" 2>&1
    return 0
}

function wait_for_health() {
    echo "Waiting for application stack to start up, by polling server health..."
    echo "Polling server at ${KD_BASE_URL}/health (timeout: ${KD_WAIT_TIMEOUT}s) ---"
    ELAPSED=0
    while true; do
      set +e
      RESPONSE=$(curl -s --cacert "${SCRIPT_DIR}/../certs/server.crt" --connect-timeout 3 "${KD_BASE_URL}/health" 2>&1)
      CURL_EXIT=$?
      set -e
      if [ ${CURL_EXIT} -ne 0 ]; then
        echo "[${ELAPSED}s] curl failed (exit ${CURL_EXIT}): ${RESPONSE}"
      else
        echo "[${ELAPSED}s] response: ${RESPONSE}"
      fi
      echo "${RESPONSE}" | grep -q '"ok"' && break
      if [ "${ELAPSED}" -ge "${KD_WAIT_TIMEOUT}" ]; then
        echo "ERROR: Server did not become healthy within ${KD_WAIT_TIMEOUT}s"
        docker compose logs
        exit 1
      fi
      sleep 3
      ELAPSED=$((ELAPSED + 3))
    done
    echo "--- Server is healthy ---"
    return 0
}

function install_test_dependencies() {
    cd "${SCRIPT_DIR}/../tests"
    npm install --prefer-offline --silent
    return 0
}

function run_tests() {
    cd "${SCRIPT_DIR}/../tests"
    npm test
    return 0
}

main "$@"
