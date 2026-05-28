#!/usr/bin/env bash
# build.sh - Build the project using the pinned Nix environment

# Ensure we are in the project root
cd "$(dirname "$0")/.."

# Define the build command
# Default to Release build for optimized performance unless specified otherwise
BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release}
BUILD_CMD="cmake -B build -GNinja -DCMAKE_BUILD_TYPE=${BUILD_TYPE} && cmake --build build"

echo "========================================================="
echo " Building Kingdom with Pinned Nix Dependencies "
echo "========================================================="

# If already inside a devbox shell, cmake and ninja are already on PATH — skip
# the nix-shell wrapper (which requires NIX_PATH / nix channels to be configured).
if [ -n "${DEVBOX_SHELL_ENABLED}" ]; then
  echo "-> devbox shell detected, running cmake directly"
  eval "${BUILD_CMD}"
else
  nix-shell ./config/build.shell.nix --run "${BUILD_CMD}"
fi

if [ $? -eq 0 ]; then
    echo "========================================================="
    echo "-> build SUCCESS"
    echo "-> Artifacts are in the 'build/' directory. "
else
    echo "========================================================="
    echo " -> build FAIL "
    echo "========================================================="
    exit 1
fi
