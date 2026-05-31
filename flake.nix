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

        # ── shared build tools (both shells) ─────────────────────────────────
        baseNativeBuildInputs = [
          pkgs.gcc15
          pkgs.cmake
          pkgs.ninja
          pkgs.pkg-config
        ];

        # ── kds runtime deps (both shells, no Qt6) ───────────────────────────
        # Qt6 intentionally excluded here — kds is a headless server.
        # Qt6 is added only in devShells.default for kdctl (the GUI client).
        baseBuildInputs = [
          # direct
          pkgs.openssl
          pkgs.libsodium
          pkgs.libpqxx
          # transitive runtime deps — explicitly listed from scripts/source-dep-report
          pkgs.nghttp3
          pkgs.libpsl
          pkgs.brotli
          pkgs.krb5
          pkgs.gcc14
          pkgs.keyutils
          pkgs.libssh2
          pkgs.libpq
          # NOTE: glibc intentionally omitted — the GCC wrapper owns its own glibc include
          # path; adding glibc to buildInputs corrupts the #include_next chain in cstdlib.
          pkgs.ngtcp2
          pkgs."publicsuffix-list"
          pkgs.nghttp2
          pkgs.libunistring
          pkgs.zlib
          pkgs.zstd
          pkgs.curl
        ];

        baseShellHook = ''
          export CXX=g++
          export CC=gcc
        '';
      in
        # ── version pins ─────────────────────────────────────────────────────
        # All packages pinned to nixos-25.11 @ ee09932cedcef15aaf476f9343d1dea2cb77e261.
        # If any assertion fails after `nix flake update`, revert flake.lock — do not remove assertions.
        # ----- Qt6 -----
        assert pkgs.qt6.qtbase.version == "6.10.1";
        assert pkgs.qt6.qttools.version == "6.10.1";
        # ----- build tools -----
        assert pkgs.gcc15.version == "15.2.0";
        assert pkgs.cmake.version == "4.1.2";
        assert pkgs.ninja.version == "1.13.1";
        assert pkgs.pkg-config.version == "0.29.2";
        # ----- direct buildInputs -----
        assert pkgs.openssl.version == "3.6.0";
        assert pkgs.libsodium.version == "1.0.20";
        assert pkgs.libpqxx.version == "7.10.1";
        # ----- transitive runtime deps -----
        assert pkgs.nghttp3.version == "1.12.0";
        assert pkgs.libpsl.version == "0.21.5";
        assert pkgs.brotli.version == "1.1.0";
        assert pkgs.krb5.version == "1.22.1";
        assert pkgs.gcc14.version == "14.3.0";
        assert pkgs.keyutils.version == "1.6.3";
        assert pkgs.libssh2.version == "1.11.1";
        assert pkgs.libpq.version == "18.1";
        assert pkgs.ngtcp2.version == "1.17.0";
        assert pkgs."publicsuffix-list".version == "0-unstable-2025-10-08";
        assert pkgs.nghttp2.version == "1.67.1";
        assert pkgs.libunistring.version == "1.4.1";
        assert pkgs.zlib.version == "1.3.1";
        assert pkgs.zstd.version == "1.5.7";
        assert pkgs.curl.version == "8.17.0";
        # NOTE: libidn2 appears in source-dep-report (v2.3.8) but is not available as a top-level
        # attribute in nixos-25.11 @ ee09932 — it is provided transitively through curl.
        {
          # ── devShells.kds — slim shell for CI and Docker ──────────────────
          # No Qt6: avoids materialising ~200 GUI-layer packages (X11, Wayland,
          # GTK, harfbuzz, ICU, fontconfig, vulkan, ...) that kds never uses.
          # Use this shell for all server-only builds:
          #   nix develop .#kds --command bash ./scripts/build.sh
          devShells.kds = pkgs.mkShell {
            nativeBuildInputs = baseNativeBuildInputs;
            buildInputs = baseBuildInputs;
            shellHook = baseShellHook + ''
              export KD_BUILD_KDCTL=OFF
            '';
          };

          # ── devShells.default — full shell for local development ──────────
          # Adds Qt6 for building kdctl (the GUI desktop client).
          # Use for local dev: nix develop  (or devbox shell)
          devShells.default = pkgs.mkShell {
            nativeBuildInputs = baseNativeBuildInputs ++ [
              pkgs.qt6.qttools            # moc, uic, rcc — needed by CMake automoc at configure time
              pkgs.qt6Packages.wrapQtAppsHook  # sets QT_PLUGIN_PATH and related env vars
            ];
            buildInputs = baseBuildInputs ++ [
              pkgs.qt6.qtbase
            ];
            shellHook = baseShellHook + ''
              # Expose Qt6 cmake config files so find_package(Qt6) works under
              # CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY without loosening system-path guards.
              export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}/lib/cmake''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
            '';
          };
        });
}
