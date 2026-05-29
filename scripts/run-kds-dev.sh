#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

main() {
    pushd "${PROJECT_DIR}"
    echo "DEBUG: Necessary vars exist."

    ./scripts/verify-env.sh
    if [[ $? -ne 0 ]]; then
        echo "Error: environment is incorrectly configured" >&2
        exit 1
    fi
    source ./scripts/verify-env.sh CRITICAL_VAR_NAMES
    export_critical_vars

    ./build/kds/kds
    popd
}

# ─── helpers ────────────────────────────────────────────────

function export_critical_vars() {
    for var in "${CRITICAL_VAR_NAMES[@]}"; do
        export $var
    done
    return 0
}

# ─────────────────────────────────────────────────────────────
main "$@"
