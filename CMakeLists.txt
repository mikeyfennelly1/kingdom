cmake_minimum_required(VERSION 3.10)
project(kingdom VERSION 1.0 LANGUAGES CXX)

find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE KD_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT KD_VERSION OR KD_VERSION STREQUAL "")
    set(KD_VERSION "unknown")
endif()
message(STATUS "Kingdom version: ${KD_VERSION}")

# HARD RULE: never search system paths
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
# Disables CMAKE_SYSTEM_PREFIX_PATH (/usr/local, /usr, etc.) so nothing leaks in from the host.
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)

# HARD RULE: only search explicitly provided prefixes
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Redirect the default /usr/local install prefix into the build tree so GNUInstallDirs
# never seeds /usr/local paths into target properties.
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "" FORCE)

# Fail if anything is missing instead of falling back
set(CMAKE_REQUIRED_QUIET OFF)

# Require explicit dependency resolution
find_package(PkgConfig REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(cmake/dependencies.cmake)

configure_third_party()
enable_testing()
option(BUILD_KDCTL "Build the kdctl GUI client (requires Qt6)" ON)
add_subdirectory(libkd)
if(BUILD_KDCTL)
    add_subdirectory(kdctl)
endif()
add_subdirectory(kds)
# Enable clang-tidy & clang-format
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=-*,readability-*,bugprone-*,modernize-*,performance-*;--warnings-as-errors=*")
endif()
find_program(CLANG_FORMAT_EXE clang-format)

# targets are defined in subdirectories kdctl and kds

