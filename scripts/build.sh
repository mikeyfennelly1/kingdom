#!/usr/bin/env bash
# build.sh - Build the project using the pinned Nix environment

# Ensure we are in the project root
cd "$(dirname "$0")/.."

# Define the build command
# We use cmake and ninja directly since go-task is excluded from the shell
BUILD_CMD="cmake -B build -GNinja && cmake --build build"

echo "========================================================="
echo " Building Kingdom with Pinned Nix Dependencies "
echo "========================================================="

# Execute the build inside the nix-shell using the local build.shell.nix
nix-shell ./config/build.shell.nix --run "${BUILD_CMD}"

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
