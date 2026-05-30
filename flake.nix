{
  description = "Kingdom build environment";

  inputs = {
    # GCC 15.2.0
    nixpkgs-gcc.url = "github:NixOS/nixpkgs/01fbdeef22b76df85ea168fbfe1bfd9e63681b30";

    # CMake 4.1.2 and Ninja 1.13.1
    nixpkgs-build.url = "github:NixOS/nixpkgs/ee09932cedcef15aaf476f9343d1dea2cb77e261";

    # OpenSSL, libsodium, libpqxx, pkg-config
    nixpkgs-libs.url = "github:NixOS/nixpkgs/d233902339c02a9c334e7e593de68855ad26c4cb";

    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs-gcc, nixpkgs-build, nixpkgs-libs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs-gcc   = nixpkgs-gcc.legacyPackages.${system};
        pkgs-build = nixpkgs-build.legacyPackages.${system};
        pkgs-libs  = nixpkgs-libs.legacyPackages.${system};
      in {
        devShells.default = pkgs-libs.mkShell {
          nativeBuildInputs = [
            pkgs-gcc.gcc
            pkgs-build.cmake
            pkgs-build.ninja
            pkgs-libs.pkg-config
          ];

          buildInputs = [
            pkgs-libs.openssl
            pkgs-libs.libsodium
            pkgs-libs.libpqxx
          ];

          shellHook = ''
            export CXX=g++
            export CC=gcc
          '';
        };
      });
}
