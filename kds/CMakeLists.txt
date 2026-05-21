file(GLOB_RECURSE KD_SERVER_SOURCES src/*.cc)

add_executable(kds
    ${KD_SERVER_SOURCES}
)

target_link_libraries(kds
    PRIVATE
        kd
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        httplib::httplib
        ${LIBPQXX_LIBRARIES}    # libpqxx - PostgreSQL C++ client
)

# libpqxx headers exposed via pkg-config (pqxx/pqxx)
target_include_directories(kds
    PRIVATE
        ../libkd/include
        ${LIBPQXX_INCLUDE_DIRS}
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
        GTest::gtest_main
)

add_test(NAME KdsSecurityTests COMMAND kds_tests)
