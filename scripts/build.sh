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
        printf "DEBUG: Error: failed to connect to GitHub — NixOS nixpkgs tarballs are unreachable" >&2
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
    printf "DEBUG: checking connectivity from build shell -> %s (required for NixOS nixpkgs tarballs)\n" "${url}"

    if ! curl -sf --connect-timeout 10 --max-time 15 "${url}" > /dev/null 2>&1; then
        printf "ERROR: unable to access %s\n" "${url}"
        return 1
    fi

    printf "DEBUG: connection to %s successful\n" "${url}"
    return 0
}

function build_project() {
    # Default to Release build for optimized performance unless specified otherwise
    BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}

    # ── cmake diagnostic knobs ───────────────────────────────────────────────
    # CMAKE_LOG_LEVEL=VERBOSE|DEBUG|TRACE   verbose configure messages
    # CMAKE_DEBUG_FIND=1                    log every find_package/find_library call
    # CMAKE_DEBUG_FIND_PKG=spdlog,openssl   log find calls for specific packages only
    #
    # Examples:
    #   CMAKE_DEBUG_FIND=1 ./scripts/build.sh 2>&1 | tee find.log
    #   CMAKE_LOG_LEVEL=VERBOSE CMAKE_DEBUG_FIND_PKG=spdlog ./scripts/build.sh
    # ────────────────────────────────────────────────────────────────────────
    CMAKE_LOG_LEVEL=${CMAKE_LOG_LEVEL:-STATUS}
    CMAKE_EXTRA_FLAGS=()
    if [[ -n "${CMAKE_DEBUG_FIND:-}" ]]; then
        CMAKE_EXTRA_FLAGS+=(--debug-find)
    elif [[ -n "${CMAKE_DEBUG_FIND_PKG:-}" ]]; then
        # --debug-find-pkg is ignored when --debug-find is already set
        CMAKE_EXTRA_FLAGS+=(--debug-find-pkg="${CMAKE_DEBUG_FIND_PKG}")
    fi
    # KD_BUILD_KDCTL=OFF is exported by devShells.kds (the slim CI/Docker shell).
    # Skips the kdctl Qt6 GUI client, which is not needed for server-only builds.
    if [[ "${KD_BUILD_KDCTL:-ON}" == "OFF" ]]; then
        CMAKE_EXTRA_FLAGS+=(-DBUILD_KDCTL=OFF)
    fi

    printf "DEBUG: outputting build artifacts to ${BUILD_DIRECTORY}\n" >&2
    cmake -B build -GNinja -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        --log-level="${CMAKE_LOG_LEVEL}" \
        "${CMAKE_EXTRA_FLAGS[@]}" \
        && cmake --build ${BUILD_DIRECTORY}

    if [ $? -eq 0 ]; then
        printf "DEBUG: -> artifacts outputted to '${BUILD_DIRECTORY}' directory..."
        export BUILD_DIRECTORY
        printf "DEBUG: contents of ${BUILD_DIRECTORY}"
        ls "${BUILD_DIRECTORY}"
        return 0
    else
        printf "DEBUG:  -> build failure. returning..."
        return 1
    fi
}
# ─── ...helpers ────────────────────────────────────────────────

main "$@"
