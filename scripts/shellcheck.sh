#!/usr/bin/env bash
set -e

VERBOSE=${VERBOSE:-false}

# Function to run tasks with gum
run_task() {
    local label="$1"
    local cmd="$2"

    if [ "$VERBOSE" = "true" ]; then
        gum log --level info "Starting $label..."
        eval "$cmd"
    else
        if gum spin --spinner dot --title "$label..." -- bash -c "$cmd" > /tmp/kingdom_shellcheck.log 2>&1; then
            gum style --foreground 2 "✔ $label"
        else
            gum style --foreground 1 "✘ $label failed"
            tail -n 20 /tmp/kingdom_shellcheck.log
            exit 1
        fi
    fi
}

# Find all .sh files and run shellcheck on them
run_task "Running ShellCheck" "find . -name '*.sh' -not -path './build/*' -not -path './.devbox/*' -not -path './.gitignored/*' | xargs shellcheck"
