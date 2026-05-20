#!/usr/bin/env bash
set -e

VERBOSE=${VERBOSE:-false}

run_task() {
    local label="$1"
    local cmd="$2"

    if [ "$VERBOSE" = "true" ]; then
        gum log --level info "Starting $label..."
        eval "$cmd"
    else
        if gum spin --spinner dot --title "$label..." -- bash -c "$cmd" > /tmp/kingdom_task.log 2>&1; then
            gum style --foreground 2 "✔ $label"
        else
            gum style --foreground 1 "✘ $label failed"
            tail -n 20 /tmp/kingdom_task.log
            exit 1
        fi
    fi
}

# Ensure build exists for tests
if [ ! -d "build" ]; then
    run_task "Configuring project" "cmake -B build -GNinja"
fi

run_task "Building tests" "cmake --build build --target kds_tests"
run_task "Running C++ tests" "cd build && ctest --output-on-failure"
