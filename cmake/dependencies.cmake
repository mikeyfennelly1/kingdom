function(configure_third_party)
    # Never let FetchContent silently redirect to a system-installed package.
    # Without this, CMake 3.24+ will call find_package() first and can pick up
    # /usr/local installs instead of downloading the pinned version.
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)

    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(json)

    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )
    FetchContent_MakeAvailable(spdlog)

    # spdlog 1.13.0 bundles fmtlib whose FMT_CONSTEVAL macro expands to
    # `consteval` on C++20 compilers. Clang ≥19 rejects this in the
    # basic_format_string constructor when called from a non-consteval context.
    # Override FMT_CONSTEVAL to constexpr to fall back to runtime format
    # checking — functionally identical for all uses in this codebase.
    if(TARGET spdlog AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "19")
        target_compile_definitions(spdlog PUBLIC "FMT_CONSTEVAL=constexpr")
    endif()

    # OpenSSL: use pkg-config to locate the split nix store paths (dev headers separate from libs),
    # then seed CMake's FindOpenSSL variables so find_package works correctly.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PC_OPENSSL REQUIRED openssl)
    find_library(OPENSSL_SSL_LIBRARY NAMES ssl HINTS ${PC_OPENSSL_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto HINTS ${PC_OPENSSL_LIBRARY_DIRS} NO_DEFAULT_PATH)
    set(OPENSSL_INCLUDE_DIR ${PC_OPENSSL_INCLUDE_DIRS} CACHE STRING "" FORCE)
    find_package(OpenSSL 3.0 REQUIRED)

    # HTTPS support enabled; macOS keychain disabled to avoid CoreFoundation dependency in Nix
    set(HTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            httplib
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
            GIT_TAG v0.15.3
    )
    FetchContent_MakeAvailable(httplib)

    FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.4.2
    )
    FetchContent_MakeAvailable(cli11)

# Test dependencies
    FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    # libpqxx - C++ PostgreSQL client, located via pkg-config since Nix doesn't ship a CMake config file for it
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPQXX REQUIRED libpqxx)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)
endfunction()
