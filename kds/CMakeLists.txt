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
        OpenSSL::SSL
        OpenSSL::Crypto
)
