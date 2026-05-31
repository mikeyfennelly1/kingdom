#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

main() {
    local build_dir="${PROJ_ROOT}/build/kds/kds"
    local out="${PROJ_ROOT}/out/kds-closure.tar.gz"
    local fetch_log="${PROJ_ROOT}/out/nix-cache-fetches.log"

    mkdir -p "${PROJ_ROOT}/out"
    : > "${fetch_log}"

    pushd "${PROJ_ROOT}"

    printf "DEBUG: cleaning build directory\n" >&2
    rm -rf "${build_dir}"

    printf "DEBUG: building artifact in nix...\n" >&2
    nix develop .#kds --command bash ./scripts/build.sh \
        2> >(tee >(grep 'cache\.nixos\.org' >> "${fetch_log}") >&2)

    mkdir -p "$(dirname "${out}")"
    printf "DEBUG: assembling nix store closure...\n" >&2

    # Run closure assembly inside nix develop so that ldd and awk are
    # guaranteed present — the nixos/nix Docker builder image does not
    # ship them on its PATH, but they come from stdenv inside the devShell.
    export _KD_BINARY="${build_dir}" _KD_OUT="${out}"
    nix develop .#kds --command bash -c '
        set -euo pipefail
        ldd "${_KD_BINARY}" \
            | awk '"'"'$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }'"'"' \
            | sort -u \
            | xargs nix-store --query --requisites \
            | sort -u \
            | tar -czPf "${_KD_OUT}" --files-from=-
    ' 2> >(tee >(grep 'cache\.nixos\.org' >> "${fetch_log}") >&2)

    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to construct closure artifact\n" >&2
        exit 1
    fi

    printf "INFO: nix cache fetch log written to %s\n" "${fetch_log}" >&2
    popd
    return 0
}

main "$@"
