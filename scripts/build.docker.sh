#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/../"
ENV_FILE="${PROJ_ROOT}/.env"
CURRENT_COMMIT_HASH=$(git rev-parse --short HEAD)
IMAGE_NAME="mikeyfennelly/kds:${CURRENT_COMMIT_HASH}"

main() {
    local extract_logs=false
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -l|--logs) extract_logs=true; shift ;;
            *) break ;;
        esac
    done

    pushd "${PROJ_ROOT}"
    trap popd EXIT

    load_env
    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to load environment\n" >&2
        exit 1
    fi

    if [[ "${extract_logs}" == true ]]; then
        extract_build_logs
        return 0
    fi

    package_runtime_docker_img
    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to package docker runtime image\n" >&2
        exit 1
    fi
    printf "DEBUG: successfully packaged docker image\n" >&2

    return 0
}

# ─── helpers ────────────────────────────────────────────────

function load_env() {
    if [[ ! -f "${ENV_FILE}" ]]; then
        printf "ERROR: ${ENV_FILE} not found — copy .env.example to .env and fill in your values." >&2
        exit 1
    fi
    
    # Load .env, expanding variable references and ignoring comments/blanks
    set -a
    # shellcheck source=/dev/null
    source "${ENV_FILE}"
    set +a
    return 0
}

function extract_build_logs() {
    local logs_dir="${PROJ_ROOT}/logs"
    mkdir -p "${logs_dir}"
    printf "Extracting build logs to ${logs_dir}\n"
    docker build "${PROJ_ROOT}" \
        --target logs \
        --output type=local,dest="${logs_dir}"
}

function package_runtime_docker_img() {
    printf "Packaging runtime Docker image...\n"
    docker build "${PROJ_ROOT}" \
        --build-arg POSTGRES_USER="${POSTGRES_USER}" \
        --build-arg POSTGRES_DB="${POSTGRES_DB}" \
        --build-arg POSTGRES_HOST="${POSTGRES_HOST:-db}" \
        --build-arg POSTGRES_PORT="${POSTGRES_PORT}" \
        --build-arg KD_TLS_CERT="${KD_TLS_CERT}" \
        --build-arg KD_JWT_TTL_SECONDS="${KD_JWT_TTL_SECONDS}" \
        --build-arg KD_LOG_LEVEL="${KD_LOG_LEVEL:-info}" \
        --build-arg KD_PORT="${KD_PORT:-8080}" \
        --tag "${IMAGE_NAME}" \
        "$@"
    return 0
}

# ─────────────────────────────────────────────────────────────
main "$@"

