#!/usr/bin/env bash

pushd ./tests || exit 1

if ! nix develop; then
    printf "unable to enter nix-shell. exiting... \n"
    exit 1
fi

have_vitest="npx vitest --version"
if ! "${have_vitest}"; then
    printf "Unable to install vitest. Exiting... \n"
    exit 1
fi

npm test
popd
