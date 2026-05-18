file(GLOB_RECURSE KD_SOURCES src/*.cc)

add_library(kd SHARED
    ${KD_SOURCES}
)

target_include_directories(kd
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Optional: control output name/location
set_target_properties(kd PROPERTIES
    OUTPUT_NAME "kd"
)
