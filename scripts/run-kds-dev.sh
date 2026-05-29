#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

CRITICAL_VAR_NAMES=(
    POSTGRES_USER
    POSTGRES_PASSWORD
    POSTGRES_DB
    POSTGRES_HOST
    POSTGRES_PORT
    KD_DB_URL
    KD_TLS_CERT
    KD_TLS_KEY
    KD_JWT_SECRET
    KD_JWT_TTL_SECONDS
)

main() {
    pushd "${PROJECT_DIR}"
    source "${PROJECT_DIR}/.env"
    check_critical_vars
    if [[ $? -ne 0 ]]; then
        echo "FATAL: one or more critical variables are missing or empty." >&2
        exit 1
    fi
    echo "DEBUG: Necessary vars exist."

    export_critical_vars

    ./build/kds/kds
    popd
}

# ─── helpers ────────────────────────────────────────────────

function check_critical_vars() {
    local missing=0
    for var in "${CRITICAL_VAR_NAMES[@]}"; do
        if [[ -z "${!var+x}" ]]; then
            echo "ERROR: required variable '$var' is not set." >&2
            missing=1
        elif [[ -z "${!var}" ]]; then
            echo "ERROR: required variable '$var' is set but empty." >&2
            missing=1
        fi
    done
    return $missing
}

function export_critical_vars() {
    for var in "${CRITICAL_VAR_NAMES[@]}"; do
        export $var
    done
    return 0
}

# ─────────────────────────────────────────────────────────────
main "$@"
