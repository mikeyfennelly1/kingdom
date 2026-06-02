#!/usr/bin/env bash
set -euo pipefail

REPO_URL="https://github.com/mikeyfennelly1/kingdom.git"
INSTALL_DIR="${HOME}/kingdom"

main() {
    echo "-> ensuring users on Debian & macOS have git, wget, and curl via brew & apt"
    ensure_dependencies

    echo "-> cloning repository"
    clone_repo

    echo "-> installing nix"
    install_nix

    echo "-> running build script for server..."
    run_build

    echo "-> running install script for kdctl..."
    run_kdctl_install

    echo "-> copying .env.example to .env"
    copy_env

    echo "-> verifying environment via script"
    run_verify_env
}

# ─── helpers ────────────────────────────────────────────────

function detect_os() {
    if [[ "$(uname)" == "Darwin" ]]; then
        echo "macos"
    elif command -v apt-get >/dev/null 2>&1; then
        echo "apt"
    else
        echo "unsupported"
    fi
}

function ensure_dependencies() {
    local os
    os="$(detect_os)"

    case "${os}" in
        macos)
            if ! command -v brew >/dev/null 2>&1; then
                echo "ERROR: Homebrew is not installed. Install it from https://brew.sh before running this script." >&2
                exit 1
            fi
            local packages=()
            command -v git  >/dev/null 2>&1 || packages+=(git)
            command -v wget >/dev/null 2>&1 || packages+=(wget)
            command -v curl >/dev/null 2>&1 || packages+=(curl)
            if [[ ${#packages[@]} -gt 0 ]]; then
                brew install "${packages[@]}"
                if [[ $? -ne 0 ]]; then
                    echo "ERROR: failed to install dependencies via brew." >&2
                    exit 1
                fi
            fi
            ;;
        apt)
            local packages=()
            command -v git  >/dev/null 2>&1 || packages+=(git)
            command -v wget >/dev/null 2>&1 || packages+=(wget)
            command -v curl >/dev/null 2>&1 || packages+=(curl)
            if [[ ${#packages[@]} -gt 0 ]]; then
                sudo apt-get update -qq
                if [[ $? -ne 0 ]]; then
                    echo "ERROR: apt-get update failed." >&2
                    exit 1
                fi
                sudo apt-get install -y "${packages[@]}"
                if [[ $? -ne 0 ]]; then
                    echo "ERROR: failed to install dependencies via apt-get." >&2
                    exit 1
                fi
            fi
            ;;
        *)
            echo "ERROR: unsupported OS. Only Debian-based Linux and macOS are supported." >&2
            exit 1
            ;;
    esac
}

function clone_repo() {
    if [[ -d "${INSTALL_DIR}/.git" ]]; then
        echo "Repository already exists at ${INSTALL_DIR}, skipping clone."
        return 0
    fi
    git clone "${REPO_URL}" "${INSTALL_DIR}"
    if [[ $? -ne 0 ]]; then
        echo "ERROR: failed to clone repository from ${REPO_URL}." >&2
        exit 1
    fi
}

function install_nix() {
    if command -v nix >/dev/null 2>&1; then
        echo "Nix is already installed, skipping."
        return 0
    fi
    sh <(curl -L https://nixos.org/nix/install) --daemon
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Nix installation failed." >&2
        exit 1
    fi
}

function run_build() {
    bash "${INSTALL_DIR}/scripts/build.sh"
    if [[ $? -ne 0 ]]; then
        echo "ERROR: build script failed." >&2
        exit 1
    fi
}

function run_kdctl_install() {
    bash "${INSTALL_DIR}/scripts/kdctl-install.sh"
    if [[ $? -ne 0 ]]; then
        echo "ERROR: kdctl install script failed." >&2
        exit 1
    fi
}

function copy_env() {
    if [[ -f "${INSTALL_DIR}/.env" ]]; then
        echo ".env already exists at ${INSTALL_DIR}/.env, skipping copy."
        return 0
    fi
    cp "${INSTALL_DIR}/.env.example" "${INSTALL_DIR}/.env"
    if [[ $? -ne 0 ]]; then
        echo "ERROR: failed to copy .env.example to .env." >&2
        exit 1
    fi
}

function run_verify_env() {
    bash "${INSTALL_DIR}/scripts/verify-env.sh"
    if [[ $? -ne 0 ]]; then
        echo "ERROR: environment verification failed. Edit ${INSTALL_DIR}/.env and re-run verify-env.sh." >&2
        exit 1
    fi
}

# ─────────────────────────────────────────────────────────────
main "$@"
