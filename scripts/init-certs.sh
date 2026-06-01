#!/usr/bin/env bash
set -euo pipefail

PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

main() {
    pushd "${PROJ_ROOT}"
    generate_tls_certs
    popd
}

# ─── helpers ────────────────────────────────────────────────
function generate_tls_certs() {
    mkdir -p certs
    openssl req -x509 -newkey rsa:4096 -sha256 -days 365 -nodes \
      -keyout certs/server.key \
      -out certs/server.crt \
      -subj "/CN=localhost" \
      -addext "subjectAltName=DNS:localhost,DNS:updakingdom.theburkenator.com,IP:127.0.0.1,IP:200.69.13.70"
    return 0
}

# ─────────────────────────────────────────────────────────────
main "$@"
