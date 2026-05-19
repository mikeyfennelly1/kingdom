file(GLOB KD_SERVER_SOURCES src/*.cc)

add_executable(kds
    ${KD_SERVER_SOURCES}
)

target_link_libraries(kds
    PRIVATE kd
)
