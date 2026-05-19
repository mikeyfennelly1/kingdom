add_library(kd SHARED
    src/Conversation.cc
    src/Message.cc
)

target_include_directories(kd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(kd PROPERTIES
    OUTPUT_NAME "kd"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)
