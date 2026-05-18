cmake_minimum_required(VERSION 3.10)
project(kingdom VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(cmake/dependencies.cmake)

configure_third_party()
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

