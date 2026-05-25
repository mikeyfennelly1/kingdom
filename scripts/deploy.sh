#!/bin/bash

source .env

ansible-playbook ansible/deploy.yml -i ansible/inventory.ini --vault-password-file="${ANSIBLE_VAULT_KEY_FILE}"
