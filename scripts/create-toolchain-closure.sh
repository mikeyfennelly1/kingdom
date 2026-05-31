#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

main() {
    local out="${PROJ_ROOT}/out/kds-toolchain.tar.gz"
    local fetch_log="${PROJ_ROOT}/out/nix-cache-fetches-toolchain.log"

    mkdir -p "${PROJ_ROOT}/out"
    : > "${fetch_log}"
    rm -rf "${out}"

    pushd "${PROJ_ROOT}"

    printf "DEBUG: assembling toolchain closure...\n" >&2
    assemble_toolchain_closure "${out}" "${fetch_log}"

    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to construct toolchain closure artifact\n" >&2
        exit 1
    fi

    printf "INFO: nix cache fetch log written to %s\n" "${fetch_log}" >&2
    popd
    return 0
}

function assemble_toolchain_closure() {
    local out="$1"
    local fetch_log="$2"

    local fetch_log_tmp
    fetch_log_tmp=$(mktemp)

    printf "DEBUG: printing dev env for devShells.kds\n" >&2
    local env_paths
    env_paths=$(nix print-dev-env "${PROJ_ROOT}#kds" \
        2> >(tee "${fetch_log_tmp}" >&2) \
        | grep -o '/nix/store/[^[:space:]:\"'"'"']*' \
        | sort -u)
    grep 'cache\.nixos\.org' "${fetch_log_tmp}" >> "${fetch_log}" || true

    printf "DEBUG: direct dev env store paths:\n%s\n" "${env_paths}" >&2

    printf "DEBUG: computing full requisite closure via nix-store\n" >&2
    local closure
    closure=$(echo "${env_paths}" | xargs nix-store --query --requisites \
        2> >(tee "${fetch_log_tmp}" >&2) \
        | sort -u)
    grep 'cache\.nixos\.org' "${fetch_log_tmp}" >> "${fetch_log}" || true
    rm -f "${fetch_log_tmp}"

    local closure_count
    closure_count=$(echo "${closure}" | wc -l)
    printf "DEBUG: toolchain closure contains %s store paths\n" "${closure_count}" >&2

    printf "DEBUG: writing toolchain closure to %s\n" "${out}" >&2
    echo "${closure}" | tar -czPf "${out}" --files-from=-
    printf "DEBUG: toolchain closure tarball written successfully\n" >&2
}

main "$@"
