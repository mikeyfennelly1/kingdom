add_library(kd SHARED
    src/Conversation.cc
    src/Message.cc
    src/Client.cc
    src/LocalKeyStore.cc
)

target_link_libraries(kd
    PUBLIC
        nlohmann_json::nlohmann_json
        httplib::httplib
        OpenSSL::SSL
        OpenSSL::Crypto
        ${LIBSODIUM_LIBRARIES}
)

target_include_directories(kd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${LIBSODIUM_INCLUDE_DIRS}
)

set_target_properties(kd PROPERTIES
    OUTPUT_NAME "kd"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)
