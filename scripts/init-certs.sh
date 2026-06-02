#!/usr/bin/env bash
set -euo pipefail

PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

main() {
    pushd "${PROJ_ROOT}"
    generate_tls_certs
    popd
}

function generate_tls_certs() {
    mkdir -p certs

    # Step 1: generate a local CA key and self-signed CA cert (CA:TRUE)
    openssl genrsa -out certs/ca.key 4096

    openssl req -x509 -new -nodes \
      -key certs/ca.key \
      -sha256 -days 3650 \
      -out certs/ca.crt \
      -subj "/CN=KingdomLocalCA"

    # Step 2: generate server key and CSR
    openssl genrsa -out certs/server.key 4096

    openssl req -new -nodes \
      -key certs/server.key \
      -out certs/server.csr \
      -subj "/CN=kingdom-server"

    # Step 3: sign the server cert with the CA
    openssl x509 -req -in certs/server.csr \
      -CA certs/ca.crt -CAkey certs/ca.key -CAcreateserial \
      -out certs/server.crt \
      -days 365 -sha256 \
      -extfile <(printf "subjectAltName=DNS:localhost,DNS:updakingdom.theburkenator.com,IP:127.0.0.1,IP:200.69.13.70\n")

    rm certs/server.csr

    echo "Certs written to certs/"
    echo "  CA cert:     certs/ca.crt  (use this in --cacert)"
    echo "  Server cert: certs/server.crt"
    echo "  Server key:  certs/server.key"
}

main "$@"
