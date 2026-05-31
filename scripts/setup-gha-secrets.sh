#!/bin/env bash
# Usage: ./setup-github-secrets.sh owner/repo

REPO=${1:-"$(gh repo view --json nameWithOwner -q .nameWithOwner)"}

echo "Setting secrets for: $REPO"

# SSH key for configure_vm (mikey user)
gh secret set CONFIGURE_SSH_KEY \
  --repo "$REPO" \
  --body "$(cat ~/.ssh/student_vm_10)"

# SSH key for deployment_vm (deployment user)
gh secret set DEPLOY_SSH_KEY \
  --repo "$REPO" \
  --body "$(cat ~/.ssh/deployment_updakingdom)"

# Ansible vault password
gh secret set ANSIBLE_VAULT_PASSWORD \
  --repo "$REPO" \
  --body "$(cat ~/.ssh/.vault_pass)"

echo "Done! Secrets set:"
gh secret list --repo "$REPO"

# Loads DOCKERHUB_USERNAME and DOCKERHUB_TOKEN from .env and pushes to GitHub secrets

set -a; source .env; set +a

REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)"

if [[ -z "$DOCKERHUB_USERNAME" || -z "$DOCKERHUB_TOKEN" ]]; then
  echo "Error: DOCKERHUB_USERNAME or DOCKERHUB_TOKEN not found in .env"
  exit 1
fi

echo "Setting DockerHub secrets for: $REPO"

gh secret set DOCKERHUB_USERNAME --repo "$REPO" --body "$DOCKERHUB_USERNAME"
gh secret set DOCKERHUB_TOKEN --repo "$REPO" --body "$DOCKERHUB_TOKEN"

echo "Done! Current secrets:"
gh secret list --repo "$REPO"

