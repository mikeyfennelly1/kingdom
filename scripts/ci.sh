#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Ports used by the test stack — must match defaults in test.sh init_test_vars
KD_PORT="${KD_PORT:-8080}"
POSTGRES_PORT="${POSTGRES_PORT:-5433}"

main() {
    echo "=== Kingdom CI ==="
    echo ""

    echo "--- Tearing down existing resources ---"
    teardown
    trap teardown EXIT

    echo ""
    echo "--- Building Docker image ---"
    build_image
    if [[ $? -ne 0 ]]; then
        echo "Error: Docker image build failed." >&2
        exit 1
    fi

    echo ""
    echo "--- Running test suite ---"
    run_tests
    if [[ $? -ne 0 ]]; then
        echo "Error: test suite failed." >&2
        exit 1
    fi

    echo ""
    echo "=== CI passed ==="
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

function build_image() {
    "${SCRIPT_DIR}/build.docker.sh"
    return $?
}

function run_tests() {
    "${SCRIPT_DIR}/test.sh" --rebuild
    return $?
}

main "$@"
