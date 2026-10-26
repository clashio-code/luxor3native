cmake_minimum_required(VERSION 3.16)
# C is required as well: device_probe is a C file.
project(luxor3_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Core: asset loading, level/track parsing, scene building, offline preview.
# No SDL2, no GL -> buildable and testable on headless CI.
# ---------------------------------------------------------------------------
add_library(luxor3_core STATIC
    core.cpp
    scene.cpp
    stb_impl.cpp
)
target_include_directories(luxor3_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(luxor3_test test_core.cpp)
target_link_libraries(luxor3_test PRIVATE luxor3_core)

add_executable(luxor3_preview tool_preview.cpp preview.cpp)
target_link_libraries(luxor3_preview PRIVATE luxor3_core)

# Read-only audit tools. They print what the parsers find in a data tree and
# never modify it; useful locally against a real installation.
add_executable(luxor3_config_audit config_audit.cpp)
target_link_libraries(luxor3_config_audit PRIVATE luxor3_core)

add_executable(luxor3_level_audit level_audit.cpp)
target_link_libraries(luxor3_level_audit PRIVATE luxor3_core)

enable_testing()
add_test(NAME core
         COMMAND luxor3_test ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/mini_game)

# ---------------------------------------------------------------------------
# Device targets.
#
# The RG40XXV image has no /dev/dri, so the video driver is decided at
# runtime. Two backends are built:
#   luxor3      SDL_Renderer 2D only. Works on every video driver (fbdev,
#               X11, Wayland) and is the safe default for this device.
#   luxor3_gl   adds the OpenGL ES 2.0 backend; falls back to 2D when a GLES2
#               context cannot be created.
#
# SDL2 is linked dynamically: the device ships SDL2 2.28.x and static-linking
# would break the video driver and joystick integration.
# ---------------------------------------------------------------------------
# SDL2 is optional: the headless core (luxor3_test, luxor3_preview) must
# configure on a machine without any SDL2 development package, because that is
# what the headless CI job does.
#
# Search explicit Ubuntu multiarch locations first. find_package/pkg-config
# may otherwise select host SDL2 instead of the target SDL2 during cross-builds.
find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0
    PATHS /usr/lib/aarch64-linux-gnu /usr/lib /usr/local/lib
    NO_CMAKE_FIND_ROOT_PATH)
find_path(SDL2_INCLUDE_DIR NAMES SDL.h
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES SDL2
    NO_CMAKE_FIND_ROOT_PATH)

if(SDL2_LIBRARY AND SDL2_INCLUDE_DIR)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
    )
else()
    # Allow host builds to use an installed CMake SDL2 package if the
    # multiarch lookup above did not find one.
    find_package(SDL2 QUIET CONFIG)
endif()

if(NOT TARGET SDL2::SDL2)
    message(STATUS "SDL2 not found: building the headless core only")
    return()
endif()

find_library(GLESV2_LIBRARY NAMES GLESv2
    PATHS /usr/lib/aarch64-linux-gnu /usr/lib
    NO_CMAKE_FIND_ROOT_PATH)

add_library(luxor3_sdl STATIC
    game.cpp
    sdl2d.cpp
)
target_include_directories(luxor3_sdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(luxor3_sdl PUBLIC luxor3_core SDL2::SDL2)

add_executable(luxor3 main.cpp)
target_link_libraries(luxor3 PRIVATE luxor3_sdl)

if(GLESV2_LIBRARY)
    add_executable(luxor3_gl main.cpp sdl_gl.cpp sprite_batch.cpp)
    target_include_directories(luxor3_gl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos
    )
    target_link_libraries(luxor3_gl PRIVATE luxor3_sdl ${GLESV2_LIBRARY})
    target_compile_definitions(luxor3_gl PRIVATE LUXOR_HAVE_GL=1)
    message(STATUS "GLES2 backend: enabled")
else()
    message(STATUS "GLES2 backend: skipped (libGLESv2 not found); 2D backend only")
endif()

# Runtime probe to run on the device. It calls glGetString, so it needs
# libGLESv2 even though it does not create its own GL renderer. The GLES2
# headers come from the vendored third_party/khronos copy, so no GLES
# development package is required on the build host.
add_executable(device_probe device_probe.c)
target_include_directories(device_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/khronos
)
if(GLESV2_LIBRARY)
    # libm is explicit because this is a C target: CMake only auto-links -lm
    # for C++ targets, and SDL2 pulls in libm math symbols (exp, pow, ...).
    target_link_libraries(device_probe PRIVATE SDL2::SDL2 ${GLESV2_LIBRARY} m)
else()
    target_link_libraries(device_probe PRIVATE SDL2::SDL2 m)
endif()
