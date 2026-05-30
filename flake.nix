{
  description = "Kingdom build environment";

  inputs = {
    # GCC 15.2.0, CMake 4.1.2, Ninja 1.13.1, libsodium 1.0.20, OpenSSL 3.6.0, libpqxx 7.10.1
    nixpkgs.url = "github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261";

    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        devShells.default = pkgs.mkShell {
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
