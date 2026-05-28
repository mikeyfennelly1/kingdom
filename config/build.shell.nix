{ pkgs ? import <nixpkgs> {} }:

let
  # Revisions sourced from devbox.lock to match versions rigorously
  # GCC 15.2.0 revision
  pkgs_gcc = import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/01fbdeef22b76df85ea168fbfe1bfd9e63681b30.tar.gz") {};
  
  # CMake 4.1.2 and Ninja 1.13.1 revision
  pkgs_build = import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/ee09932cedcef15aaf476f9343d1dea2cb77e261.tar.gz") {};
  
  # OpenSSL, libsodium, libpqxx, and pkg-config revision
  pkgs_libs = import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/d233902339c02a9c334e7e593de68855ad26c4cb.tar.gz") {};
in
pkgs.mkShell {
  # nativeBuildInputs contains tools required for the build process
  nativeBuildInputs = [
    pkgs_gcc.gcc
    pkgs_build.cmake
    pkgs_build.ninja
    pkgs_libs.pkg-config
  ];

  # buildInputs contains the libraries the project links against
  buildInputs = [
    pkgs_libs.openssl
    pkgs_libs.libsodium
    pkgs_libs.libpqxx
  ];

  shellHook = ''
    export CXX=g++
    export CC=gcc
    
    echo "---------------------------------------------------------"
    echo " Kingdom Rigorous Build Shell "
    echo "---------------------------------------------------------"
    echo " GCC:     $(g++ --version | head -n 1)"
    echo " CMake:   $(cmake --version | head -n 1)"
    echo " Ninja:   $(ninja --version | head -n 1)"
    echo " OpenSSL: $(openssl version)"
    echo "---------------------------------------------------------"
  '';
}
