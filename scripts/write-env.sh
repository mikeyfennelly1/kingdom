#!/bin/bash

# This script generates a .env file with default values suitable for 
# the deployment stack described in .ansible/deploy.yml.
# It allows overriding values via environment variables.

ENV_FILE=".env"

# Default values
KD_PORT=${KD_PORT:-8080}
KD_LOG_LEVEL=${KD_LOG_LEVEL:-debug}
POSTGRES_PASSWORD=${POSTGRES_PASSWORD:-postgres}
POSTGRES_USER=${POSTGRES_USER:-postgres}
POSTGRES_DB=${POSTGRES_DB:-kingdom}
POSTGRES_PORT=${POSTGRES_PORT:-5432}
POSTGRES_HOST=${POSTGRES_HOST:-db}

# Generate the .env file
cat <<EOF > "$ENV_FILE"
KD_PORT=$KD_PORT
KD_LOG_LEVEL=$KD_LOG_LEVEL
POSTGRES_PASSWORD=$POSTGRES_PASSWORD
POSTGRES_USER=$POSTGRES_USER
POSTGRES_DB=$POSTGRES_DB
POSTGRES_PORT=$POSTGRES_PORT
POSTGRES_HOST=$POSTGRES_HOST
EOF

echo "Generated $ENV_FILE with the following values:"
cat "$ENV_FILE"
