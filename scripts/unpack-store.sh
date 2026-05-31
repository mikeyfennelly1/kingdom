#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

main() {
    local UNPACK_LOCATION="${PROJ_ROOT}/unpacked-store"

    if [[ $# -eq 0 ]]; then
        printf "ERROR: usage: unpack-store.sh <tarball> [<tarball> ...]\n" >&2
        exit 1
    fi

    mkdir -p "${UNPACK_LOCATION}"

    for tarball in "$@"; do
        store_must_exist "${tarball}"
        printf "DEBUG: unpacking %s to %s\n" "${tarball}" "${UNPACK_LOCATION}" >&2
        printf "DEBUG: entries in tarball: $(tar -tf "${tarball}" | wc -l)\n" >&2
        tar -xf "${tarball}" -C "${UNPACK_LOCATION}" >/dev/null
        printf "DEBUG: %s unpacked successfully\n" "${tarball}" >&2
    done

    printf "DEBUG: unpacked-store entry count: $(ls "${UNPACK_LOCATION}" | wc -l)\n" >&2
}

function store_must_exist() {
    if ! stat "$1" > /dev/null 2>&1; then
        printf "ERROR: %s not found.\n" "$1" >&2
        exit 1
    fi
}

main "$@"
