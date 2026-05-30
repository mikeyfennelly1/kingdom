#!/usr/bin/env bash
# build.sh - Build the project using the pinned Nix environment
set -euo pipefail

main() {
    echo " check connection to nix packages..."
    check_nixos_packages_connection
    if [[ $? -ne 0 ]]; then
        echo "Error: failed to connect to GitHub — NixOS nixpkgs tarballs are unreachable" >&2
        exit 1
    fi

    echo " Building Kingdom with Pinned Nix Dependencies in nix shell"
    build_project
    if [[ $? -ne 0 ]]; then
        echo "FATAL: BUILD FAILURE.. exiting" >&2
        exit 1
    fi
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


function orient_script() {
    # Ensure we are in the project root
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "${SCRIPT_DIR}/.."
    return 0
}

function build_project() {
    # Define the build command
    # Default to Release build for optimized performance unless specified otherwise
    BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    BUILD_DIRECTORY="${SCRIPT_DIR}/../build"
    echo "outputting build artifacts to ${BUILD_DIRECTORY}"
    BUILD_CMD="cmake -B build -GNinja -DCMAKE_BUILD_TYPE=${BUILD_TYPE} && cmake --build ${BUILD_DIRECTORY}"

    # Execute the build inside the nix-shell using the local build.shell.nix
    nix-shell ./config/build.shell.nix --run "${BUILD_CMD}"

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
