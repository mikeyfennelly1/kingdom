cmake_minimum_required(VERSION 3.10)
include(FetchContent)

project(kingdom VERSION 1.0 LANGUAGES CXX)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# Enable clang-tidy
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=-*,readability-*,bugprone-*,modernize-*,performance-*;--warnings-as-errors=*")
endif()

# Source files
set(SOURCES
    src/main.cpp
)

# Add executable target
add_executable(kingdom ./src/main.cc)

target_include_directories(kingdom PRIVATE ${httplib_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/src)

target_link_libraries(kingdom
    PRIVATE 
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    gtest
    gtest_main 
    httplib::httplib 
    OpenSSL::SSL 
    OpenSSL::Crypto
)

install(TARGETS kingdom DESTINATION bin)
