{
  description = "Kingdom build environment";

  inputs = {
    # Pinned to nixos-25.11 @ ee09932cedcef15aaf476f9343d1dea2cb77e261.
    # See https://github.com/NixOS/nixpkgs/tree/ee09932cedcef15aaf476f9343d1dea2cb77e261
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";

    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        devShells.default =
          # gcc15 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.gcc15.version == "15.2.0";
          # cmake pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.cmake.version == "4.1.2";
          # ninja pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.ninja.version == "1.13.1";
          # pkg-config pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.pkg-config.version == "0.29.2";
          # openssl pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.openssl.version == "3.6.0";
          # libsodium pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libsodium.version == "1.0.20";
          # libpqxx pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libpqxx.version == "7.10.1";
          pkgs.mkShell {
            nativeBuildInputs = [
              pkgs.gcc15
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
            ];

            buildInputs = [
              pkgs.openssl
              pkgs.libsodium
              pkgs.libpqxx
            ];

            shellHook = ''
              export CXX=g++
              export CC=gcc
            '';
          };
      });
}
