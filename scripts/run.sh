#!/usr/bin/env bash

# 1. Enter devbox
# If not already in devbox shell, run this script inside devbox
if [ -z "$DEVBOX_SHELL_ENABLED" ]; then
    echo "Entering devbox..."
    exec devbox run -- bash "$0" "$@"
fi

# 2. Source environment variables
if [ -f .env ]; then
    echo "Sourcing .env..."
    # Exporting variables from .env for the current script
    set -a
    source .env
    set +a
fi

# 3. Clean the build directory
echo "Cleaning build directory..."
rm -rf build

# 4. Setup tmux session with 2 vertical panes
SESSION="kingdom"

# Kill existing session if it exists to ensure a clean start
tmux kill-session -t "$SESSION" 2>/dev/null

# Create a new session, detached, named 'kingdom'
tmux new-session -d -s "$SESSION" -n 'workspace'

# Enable pane titles
tmux set-option -t "$SESSION" pane-border-status top

# First pane (left) will be 'db'
tmux select-pane -t "$SESSION:0.0" -T "db"

# Split vertically to create the second pane (right), which will be 'kds'
tmux split-window -h -t "$SESSION:0"
tmux select-pane -t "$SESSION:0.1" -T "kds"

# 6. Start postgres via docker compose up db in the 'db' pane
tmux send-keys -t "$SESSION:0.0" "docker compose up db" C-m

# 5. Source .env, build with cmake and start the server binary in the 'kds' pane
tmux send-keys -t "$SESSION:0.1" "source .env && cmake -B build -GNinja && cmake --build build && ./build/kds/kds" C-m

# Attach to the session
echo "Attaching to tmux session '$SESSION'..."
tmux attach-session -t "$SESSION"
