function(configure_third_party)
    FetchContent_Declare(json 
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz 
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(json)
    find_package(OpenSSL 3.0 REQUIRED)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )
    FetchContent_MakeAvailable(spdlog)
    set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
            httplib
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
            GIT_TAG v0.15.3
    )
    FetchContent_MakeAvailable(httplib)

# Test dependencies
    FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endfunction()
