#!/usr/bin/env bash
# build.sh - Build the project using the pinned Nix environment
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."
BUILD_DIRECTORY="${SCRIPT_DIR}/../build"

main() {
    printf "DEBUG: entering build environment\n" >&2
    pushd "${PROJ_ROOT}"
    mkdir -p build

    printf "DEBUG: check connection to nix packages...\n" >&2
    check_nixos_packages_connection
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to connect to GitHub — NixOS nixpkgs tarballs are unreachable" >&2
        exit 1
    fi

    printf "DEBUG: Building Kingdom with Pinned Nix Dependencies in nix shell\n" >&2
    build_project
    if [[ $? -ne 0 ]]; then
        printf "FATAL: BUILD FAILURE.. exiting" >&2
        exit 1
    fi
    return 0
}

# ─── helpers... ────────────────────────────────────────────────

# ensures that nix package registry is reacheable 
# before script execution.
function check_nixos_packages_connection() {
    local url="https://github.com"
    printf "checking connectivity from build shell -> %s (required for NixOS nixpkgs tarballs)\n" "${url}"

    if ! curl -sf --connect-timeout 10 --max-time 15 "${url}" > /dev/null 2>&1; then
        printf "unable to access %s\n" "${url}"
        return 1
    fi

    printf "connection to %s successful\n" "${url}"
    return 0
}

function build_project() {
    # Define the build command
    # Default to Release build for optimized performance unless specified otherwise
    BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}
    printf "DEBUG: outputting build artifacts to ${BUILD_DIRECTORY}\n" >&2
    BUILD_CMD="cmake -B build -GNinja -DCMAKE_BUILD_TYPE=${BUILD_TYPE} && cmake --build ${BUILD_DIRECTORY}"

    if [ $? -eq 0 ]; then
        echo "-> artifacts outputted to '${BUILD_DIRECTORY}' directory..."
        export BUILD_DIRECTORY
        echo "contents of ${BUILD_DIRECTORY}"
        ls "${BUILD_DIRECTORY}"
        return 0
    else
        echo " -> build failure. returning..."
        return 1
    fi
}
# ─── ...helpers ────────────────────────────────────────────────

main "$@"
