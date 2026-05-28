#!/usr/bin/env bash

binary="$1"

ldd "$binary" | awk '{print $3}' | grep -v '^/nix/store' | grep -q . && {
  echo "WARNING: Non-Nix dependency detected"
  ldd "$binary"
}

exit 0
