#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Ports used by the test stack — must match defaults in test.sh init_test_vars
KD_PORT="${KD_PORT:-8080}"
POSTGRES_PORT="${POSTGRES_PORT:-5433}"

main() {
    bash -c "${SCRIPT_DIR}/build.docker.sh"
    bash -c "${SCRIPT_DIR}/test.sh" || printf "ERROR: test suite failed.\n" >&2
    local commit_hash="$(git rev-parse --short HEAD)"
    bash -c "docker push mikeyfennelly/kds:${commit_hash}"
    bash -c "docker push mikeyfennelly/kds:latest"
    bash "${SCRIPT_DIR}/deploy.sh" "${commit_hash}"
}

main "$@"
