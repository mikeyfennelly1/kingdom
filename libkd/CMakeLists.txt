# Static linking: libkd is only ever consumed by kds and kdctl within the same
# image — no process-sharing benefit to a .so. Shared linking caused
# libkd.so.1 to be missing at runtime because the Docker build only copies
# the kds binary and the Nix store closure (project-local .so files are not
# captured by the ldd/closure script and are never explicitly staged).
add_library(kd STATIC
    src/Conversation.cc
    src/Message.cc
    src/MessageStore.cc
    src/Client.cc
    src/LocalKeyStore.cc
    src/User.cc
)

add_custom_command(TARGET kd POST_BUILD
  COMMAND ${CMAKE_SOURCE_DIR}/cmake/check-nix-linkage.sh
          $<TARGET_FILE:kd>
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
)
