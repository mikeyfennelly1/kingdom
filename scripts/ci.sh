#!/usr/bin/env bash
set -e

# Master CI script
if command -v gum >/dev/null 2>&1; then
    gum style --border normal --margin "1 2" --padding "1 2" --foreground 4 "Kingdom CI Suite"
else
    echo "=== Kingdom CI Suite ==="
fi

export VERBOSE=${VERBOSE:-false}

./scripts/shellcheck.sh
./scripts/static-analysis.sh
./scripts/build.sh
./scripts/test.sh

if command -v gum >/dev/null 2>&1; then
    gum style --foreground 2 --bold "ALL CHECKS PASSED"
else
    echo "ALL CHECKS PASSED"
fi
