# Allow AUTOMOC/AUTOUIC to process .hh files (same as .h).
cmake_policy(SET CMP0100 NEW)

# Locate Qt6 prefix.
# In devbox/Nix, Qt6_DIR or Qt6_PREFIX should be set in the environment.
# On macOS with Homebrew, Qt lives at /opt/homebrew/opt/qt.
# We append the prefix to CMAKE_FIND_ROOT_PATH so that Qt6 is discoverable
# even under the strict CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY policy set
# by the top-level CMakeLists.

if(DEFINED ENV{Qt6_PREFIX})
    list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{Qt6_PREFIX}")
elseif(DEFINED ENV{Qt6_DIR})
    # Qt6_DIR typically points to .../lib/cmake/Qt6; go three levels up to get prefix.
    get_filename_component(_qt6_cmake_dir "$ENV{Qt6_DIR}" DIRECTORY)
    get_filename_component(_qt6_lib_dir "${_qt6_cmake_dir}" DIRECTORY)
    get_filename_component(_qt6_prefix "${_qt6_lib_dir}" DIRECTORY)
    list(APPEND CMAKE_FIND_ROOT_PATH "${_qt6_prefix}")
elseif(EXISTS "/opt/homebrew/opt/qt")
    list(APPEND CMAKE_FIND_ROOT_PATH "/opt/homebrew/opt/qt")
endif()

# Qt6Gui depends on WrapOpenGL, which lives in macOS system frameworks.
# Relax the strict ONLY find-root-path mode for this subdirectory so that
# cmake can locate system frameworks (OpenGL, etc.) that Qt requires.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# macOS: the root CMakeLists disables CMAKE_FIND_USE_CMAKE_SYSTEM_PATH globally,
# which prevents FindOpenGL from finding /System/Library/Frameworks.
# Re-enable it here, scoped to this subdirectory.
if(APPLE)
    set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH TRUE)
    list(APPEND CMAKE_FRAMEWORK_PATH "/System/Library/Frameworks")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

file(GLOB KD_CTL_SOURCES src/*.cc)

add_executable(kdctl
    ${KD_CTL_SOURCES}
)

set_target_properties(kdctl PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

target_link_libraries(kdctl
    PRIVATE
        kd
        Qt6::Core
        Qt6::Widgets
        spdlog::spdlog
)
