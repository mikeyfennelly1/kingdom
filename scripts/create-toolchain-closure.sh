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

    export _KD_OUT="${out}"
    local closure_log
    closure_log=$(mktemp)
    nix develop "${PROJ_ROOT}#kds" --command bash -c '
        set -euo pipefail

        printf "DEBUG: resolving nativeBuildInputs store paths via which\n" >&2
        tool_roots=$(for tool in cmake ninja gcc pkg-config; do
            tool_path=$(which "$tool" 2>/dev/null || true)
            if [[ -n "$tool_path" ]]; then
                echo "$tool_path" | grep -o '"'"'/nix/store/[^/]*'"'"'
            fi
        done | sort -u)
        printf "DEBUG: tool roots:\n%s\n" "${tool_roots}" >&2

        printf "DEBUG: computing full requisite closure via nix-store\n" >&2
        closure=$(echo "${tool_roots}" | xargs nix-store --query --requisites | sort -u)
        closure_count=$(echo "${closure}" | wc -l)
        printf "DEBUG: toolchain closure contains %s store paths\n" "${closure_count}" >&2

        printf "DEBUG: writing toolchain closure to %s\n" "${_KD_OUT}" >&2
        echo "${closure}" | tar -czPf "${_KD_OUT}" --files-from=-
        printf "DEBUG: toolchain closure tarball written successfully\n" >&2
    ' 2>"${closure_log}"
    cat "${closure_log}" >&2
    grep 'cache\.nixos\.org' "${closure_log}" >> "${fetch_log}" || true
    rm -f "${closure_log}"
}

main "$@"
