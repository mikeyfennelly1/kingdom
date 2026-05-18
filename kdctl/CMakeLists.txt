file(GLOB KD_CTL_SOURCES src/*.cc)

add_executable(kdctl
    ${KD_CTL_SOURCES}
)

target_link_libraries(kdctl
    PRIVATE kd
)

target_include_directories(kdctl
    PRIVATE ../libkd/include
)
