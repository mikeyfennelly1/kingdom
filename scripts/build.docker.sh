#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/../"
ENV_FILE="${PROJ_ROOT}/.env"

main() {
    pushd "${PROJ_ROOT}"
    trap popd EXIT

    printf "DEBUG: executing create-closure script\n" >&2
    bash -c "${SCRIPT_DIR}/create-closure.sh"
    if [[ $? -ne 0 ]]; then
        printf "ERROR: create-closure script failed\n" >&2
        exit 1
    fi
    printf "DEBUG: closure creation script executed successfully\n" >&2

    load_env
    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to load environment\n" >&2
        exit 1
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

function package_runtime_docker_img() {
    local commit_hash="$(git rev-parse --short HEAD)"
    local image_namespace="mikeyfennelly/kds"
    local image_name_git_sha="${image_namespace}:${commit_hash}"
    printf "DEBUG: packaging runtime Docker image as: ${image_name_git_sha}\n"
    docker build "${PROJ_ROOT}" \
        --build-arg POSTGRES_USER="${POSTGRES_USER}" \
        --build-arg POSTGRES_DB="${POSTGRES_DB}" \
        --build-arg POSTGRES_HOST="${POSTGRES_HOST:-db}" \
        --build-arg POSTGRES_PORT="${POSTGRES_PORT}" \
        --build-arg KD_TLS_CERT="${KD_TLS_CERT}" \
        --build-arg KD_JWT_TTL_SECONDS="${KD_JWT_TTL_SECONDS}" \
        --build-arg KD_LOG_LEVEL="${KD_LOG_LEVEL:-info}" \
        --build-arg KD_PORT="${KD_PORT:-8080}" \
        --tag "${image_name_git_sha}" \
        "$@"
    local latest_img_name="${image_namespace}:latest"
    docker tag "${image_name_git_sha}" "${latest_img_name}"

    printf "INFO: images tagged at...\n" >&2
    printf "\t -> ${image_name_git_sha}\n" >&2
    printf "\t -> ${latest_img_name}\n" >&2

    return 0
}

# ─────────────────────────────────────────────────────────────
main "$@"

