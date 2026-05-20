#!/bin/bash

# kdctl-install.sh
# Installs the kdctl binary to the system path.

set -e

# Configuration
BINARY_NAME="kdctl"
BUILD_PATH="./build/kdctl/kdctl"
INSTALL_DIR="/usr/local/bin"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}==>${NC} Installing ${BINARY_NAME}..."

# 1. Check if binary exists
if [ ! -f "$BUILD_PATH" ]; then
    echo -e "${RED}Error:${NC} Binary not found at ${BUILD_PATH}."
    echo -e "Please run 'task build' first to compile the project."
    exit 1
fi

# 2. Determine if sudo is needed
if [ -w "$INSTALL_DIR" ]; then
    SUDO=""
else
    echo -e "${BLUE}==>${NC} Elevation required to write to ${INSTALL_DIR}"
    SUDO="sudo"
fi

# 3. Perform installation
echo -e "${BLUE}==>${NC} Copying ${BINARY_NAME} to ${INSTALL_DIR}/${BINARY_NAME}..."
$SUDO cp "$BUILD_PATH" "${INSTALL_DIR}/${BINARY_NAME}"
$SUDO chmod +x "${INSTALL_DIR}/${BINARY_NAME}"

# 4. Verify installation
if command -v "$BINARY_NAME" >/dev/null 2>&1; then
    VERSION=$("$BINARY_NAME" --version 2>/dev/null || echo "1.0")
    echo -e "${GREEN}Success!${NC} ${BINARY_NAME} (v${VERSION}) installed to ${INSTALL_DIR}"
    echo -e "Try running: ${BLUE}${BINARY_NAME} --help${NC}"
else
    echo -e "${RED}Warning:${NC} Installation completed, but ${BINARY_NAME} is not in your PATH."
    echo -e "Ensure ${INSTALL_DIR} is in your environment PATH."
fi
