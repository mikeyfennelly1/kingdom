#!/bin/env bash

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

main() {
    local build_dir="./build/kds/kds"
    local out="./out/kds-closure.tar.gz"
    pushd "${PROJ_ROOT}"
    
    printf "DEBUG: cleaning build directory\n" >&2
    rm -rf "${build_dir}"

    printf "DEBUG: building artifact in nix...\n" >&2
    nix develop --command bash ./scripts/build.sh

    ldd "${build_dir}" \
              | awk '$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }' \
              | sort -u \
              | xargs nix-store --query --requisites \
              | sort -u \
              | tar -czPf "${out}" --files-from=-
    if [[ $? -ne 0 ]]; then
        printf "ERROR: failed to construct closure artifact\n" >&2
        exit 1
    fi
    popd
    return 0
}

# ─── helpers ────────────────────────────────────────────────


# ─────────────────────────────────────────────────────────────
main "$@"

