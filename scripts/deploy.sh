#!/bin/bash

source .env

REPO_VERSION=${1:-"$(git rev-parse --short HEAD)"}

echo "deploying version: $REPO_VERSION"
echo "using key at: $ANSIBLE_VAULT_KEY_FILE"

ansible-playbook ansible/deploy.yml \
  -i ansible/inventory.ini \
  --extra-vars "repo_version=$REPO_VERSION" \
  --vault-password-file="${ANSIBLE_VAULT_KEY_FILE}"
