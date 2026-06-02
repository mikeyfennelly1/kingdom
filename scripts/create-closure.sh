#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

main() {
    local kds_binary="${PROJ_ROOT}/build/kds/kds"
    local out="${PROJ_ROOT}/out/kds-closure.tar.gz"
    local fetch_log="${PROJ_ROOT}/out/nix-cache-fetches.log"

    mkdir -p "${PROJ_ROOT}/out"
    : > "${fetch_log}"

    pushd "${PROJ_ROOT}"

    printf "DEBUG: cleaning build directory\n" >&2
    rm -rf "${kds_binary}"
    rm -rf "${out}"

    printf "DEBUG: building artifact in nix...\n" >&2
    local build_log
    build_log=$(mktemp)
    nix develop .#kds --command bash ./scripts/build.sh 2>"${build_log}"
    cat "${build_log}" >&2
    grep 'cache\.nixos\.org' "${build_log}" >> "${fetch_log}" || true
    rm -f "${build_log}"

    printf "DEBUG: copying kds binary to out/\n" >&2
    cp "${kds_binary}" "${PROJ_ROOT}/out/kds"

    printf "DEBUG: assembling nix store closure...\n" >&2
    analyse_binary_requisites "${kds_binary}" "${out}" "${fetch_log}"

    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to construct closure artifact\n" >&2
        exit 1
    fi

    printf "INFO: nix cache fetch log written to %s\n" "${fetch_log}" >&2
    popd
    return 0
}

function analyse_binary_requisites() {
    local kds_binary="$1"
    local out="$2"
    local fetch_log="$3"

    export _KD_BINARY="${kds_binary}" _KD_OUT="${out}"
    printf "DEBUG: running ldd against ${_KD_BINARY}\n" >&2
    # Run closure assembly inside nix develop so that ldd and awk are
    # guaranteed present — the nixos/nix Docker builder image does not
    # ship them on its PATH, but they come from stdenv inside the devShell.
    local closure_log
    closure_log=$(mktemp)
    nix develop .#kds --command bash -c '
        set -euo pipefail

        printf "DEBUG: resolving direct /nix/store deps via ldd\n" >&2
        direct_deps=$(ldd "${_KD_BINARY}" \
            | awk '"'"'$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }'"'"' \
            | sort -u)
        printf "DEBUG: direct deps:\n%s\n" "${direct_deps}" >&2

        printf "DEBUG: computing full requisite closure via nix-store\n" >&2
        closure=$(echo "${direct_deps}" | xargs nix-store --query --requisites | sort -u)
        closure_count=$(echo "${closure}" | wc -l)
        printf "DEBUG: closure contains %s store paths\n" "${closure_count}" >&2

        printf "DEBUG: writing closure to %s\n" "${_KD_OUT}" >&2
        echo "${closure}" | tar -czPf "${_KD_OUT}" --files-from=-
        printf "DEBUG: closure tarball written successfully\n" >&2
    ' 2>"${closure_log}"
    cat "${closure_log}" >&2
    grep 'cache\.nixos\.org' "${closure_log}" >> "${fetch_log}" || true
    rm -f "${closure_log}"
}

main "$@"
