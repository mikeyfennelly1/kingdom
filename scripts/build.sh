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

run_task "Configuring project" "cmake -B build -GNinja"
run_task "Building binaries" "cmake --build build"
run_task "Building Docker image" "docker compose build"
