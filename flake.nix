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
          # ----- Qt6 -----
          # qt6.qtbase pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.qt6.qtbase.version == "6.10.1";
          # qt6.qttools pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.qt6.qttools.version == "6.10.1";
          # ----- build tools (nativeBuildInputs) -----
          # gcc15 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.gcc15.version == "15.2.0";
          # cmake pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.cmake.version == "4.1.2";
          # ninja pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.ninja.version == "1.13.1";
          # pkg-config pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.pkg-config.version == "0.29.2";
          # ----- direct buildInputs -----
          # openssl pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.openssl.version == "3.6.0";
          # libsodium pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libsodium.version == "1.0.20";
          # libpqxx pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libpqxx.version == "7.10.1";
          # ----- transitive runtime deps (from scripts/source-dep-report) -----
          # nghttp3 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.nghttp3.version == "1.12.0";
          # libpsl pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libpsl.version == "0.21.5";
          # brotli pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.brotli.version == "1.1.0";
          # krb5 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.krb5.version == "1.22.1";
          # gcc14 pinned at nixos-25.11 (provides gcc-14.x runtime libs pulled in transitively by curl/libpq)
          # — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.gcc14.version == "14.3.0";
          # keyutils pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.keyutils.version == "1.6.3";
          # libssh2 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libssh2.version == "1.11.1";
          # libpq pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libpq.version == "18.1";
          # glibc pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.glibc.version == "2.40";
          # ngtcp2 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.ngtcp2.version == "1.17.0";
          # publicsuffix-list pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs."publicsuffix-list".version == "0-unstable-2025-10-08";
          # nghttp2 pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.nghttp2.version == "1.67.1";
          # libunistring pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.libunistring.version == "1.4.1";
          # zlib pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.zlib.version == "1.3.1";
          # zstd pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.zstd.version == "1.5.7";
          # curl pinned at nixos-25.11 — if this fails after `nix flake update`, revert flake.lock, do not remove the assertion.
          assert pkgs.curl.version == "8.17.0";
          # NOTE: libidn2 appears in source-dep-report (v2.3.8) but is not available as a top-level
          # attribute in nixos-25.11 @ ee09932 — it is provided transitively through curl.
          pkgs.mkShell {
            nativeBuildInputs = [
              pkgs.gcc15
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
              pkgs.qt6.qttools            # moc, uic, rcc — needed by CMake automoc at configure time
              pkgs.qt6Packages.wrapQtAppsHook  # sets QT_PLUGIN_PATH and related env vars
            ];

            buildInputs = [
              # Qt6
              pkgs.qt6.qtbase
              # direct
              pkgs.openssl
              pkgs.libsodium
              pkgs.libpqxx
              # transitive runtime deps — explicitly listed from source-dep-report
              pkgs.nghttp3
              pkgs.libpsl
              pkgs.brotli
              pkgs.krb5
              pkgs.gcc14
              pkgs.keyutils
              pkgs.libssh2
              pkgs.libpq
              pkgs.glibc
              pkgs.ngtcp2
              pkgs."publicsuffix-list"
              pkgs.nghttp2
              pkgs.libunistring
              pkgs.zlib
              pkgs.zstd
              pkgs.curl
            ];

            shellHook = ''
              export CXX=g++
              export CC=gcc
              # Expose Qt6 cmake config files so find_package(Qt6) works under
              # CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY without loosening system-path guards.
              export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}/lib/cmake''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
            '';
          };
      });
}
