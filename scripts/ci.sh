#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Ports used by the test stack — must match defaults in test.sh init_test_vars
KD_PORT="${KD_PORT:-8080}"
POSTGRES_PORT="${POSTGRES_PORT:-5433}"

SKIP_TESTS=false

usage() {
    printf "Usage: %s [--skip-tests]\n" "$(basename "$0")" >&2
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --skip-tests) SKIP_TESTS=true ;;
            *) usage; exit 1 ;;
        esac
        shift
    done
}

main() {
    parse_args "$@"

    bash -c "${SCRIPT_DIR}/create-closure.sh"
    if [[ $? -ne 0 ]]; then
        printf "ERROR: create-closure script failed\n" >&2
        exit 1
    fi
    bash -c "${SCRIPT_DIR}/build.docker.sh"
    if [[ "${SKIP_TESTS}" == false ]]; then
        bash -c "${SCRIPT_DIR}/test.sh" || printf "ERROR: test suite failed.\n" >&2
    else
        printf "INFO: skipping tests\n"
    fi
    local commit_hash="$(git rev-parse --short HEAD)"
    bash -c "docker push mikeyfennelly/kds:${commit_hash}"
    bash -c "docker push mikeyfennelly/kds:latest"
    bash "${SCRIPT_DIR}/deploy.sh" "${commit_hash}"
}

main "$@"
