set(PKG_CONFIG_EXECUTABLE $ENV{PKG_CONFIG})

file(GLOB_RECURSE KD_SERVER_SOURCES src/*.cc)

add_executable(kds
    ${KD_SERVER_SOURCES}
)

add_custom_command(TARGET kds POST_BUILD
  COMMAND ${CMAKE_SOURCE_DIR}/cmake/check-nix-linkage.sh
          $<TARGET_FILE:kds>
)

target_link_libraries(kds
    PRIVATE
        kd
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        httplib::httplib
        ${LIBPQXX_LIBRARIES}    # libpqxx - PostgreSQL C++ client
        ${LIBSODIUM_LIBRARIES}  # libsodium - Argon2id password hashing
        OpenSSL::SSL
        OpenSSL::Crypto
)

# libpqxx/libsodium headers exposed via pkg-config
target_include_directories(kds
    PRIVATE
        ../libkd/include
        ${LIBPQXX_INCLUDE_DIRS}
        ${LIBSODIUM_INCLUDE_DIRS}
)

# Testing
add_executable(kds_tests
    tests/SecurityTests.cc
    src/security/SecurityPredicateFactory.cc
    src/security/SecurityFilterChain.cc
)

target_link_libraries(kds_tests
    PRIVATE
        kd
        spdlog::spdlog
        httplib::httplib
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
        GTest::gtest_main
)

add_test(NAME KdsSecurityTests COMMAND kds_tests)
