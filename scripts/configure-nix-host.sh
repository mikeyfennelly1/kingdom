#!/usr/bin/env bash
# configure-nix-host.sh
#
# Configures /etc/nix/nix.conf for the current host.
# Required before running any nix develop / nix build commands in environments
# where nix.conf is not pre-configured (e.g. the nixos/nix Docker builder image).
#
# Idempotent: checks before appending so re-runs are safe.
set -euo pipefail

NIX_CONF="/etc/nix/nix.conf"

append_if_missing() {
    local line="$1"
    grep -qF "${line}" "${NIX_CONF}" 2>/dev/null || echo "${line}" >> "${NIX_CONF}"
}

# Flakes and the nix command are required by the project's flake.nix.
append_if_missing "experimental-features = nix-command flakes"

# Nix's build sandbox requires kernel namespaces that are unavailable inside
# Docker containers — disable it so nix develop / nix build can run.
append_if_missing "sandbox = false"
