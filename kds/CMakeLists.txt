file(GLOB KD_SERVER_SOURCES src/*.cc)

add_executable(kds
    ${KD_SERVER_SOURCES}
)

target_link_libraries(kds
    PRIVATE kd
)

target_include_directories(kds
    PRIVATE ../libkd/include
)
