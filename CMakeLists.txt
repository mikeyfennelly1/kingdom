cmake_minimum_required(VERSION 3.10)
project(kingdom VERSION 1.0 LANGUAGES CXX)

# HARD RULE: never search system paths
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)

# HARD RULE: only search explicitly provided prefixes
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Fail if anything is missing instead of falling back
set(CMAKE_REQUIRED_QUIET OFF)

# Require explicit dependency resolution
find_package(PkgConfig REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(cmake/dependencies.cmake)

configure_third_party()
enable_testing()
add_subdirectory(libkd)
add_subdirectory(kdctl)
add_subdirectory(kds)
# Enable clang-tidy & clang-format
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=-*,readability-*,bugprone-*,modernize-*,performance-*;--warnings-as-errors=*")
endif()
find_program(CLANG_FORMAT_EXE clang-format)

# targets are defined in subdirectories kdctl and kds

