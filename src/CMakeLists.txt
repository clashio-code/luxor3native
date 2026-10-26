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
# pkg-config comes first because it is sysroot-aware: PKG_CONFIG_LIBDIR can
# point at the arm64 pkgconfig directory so a cross build picks the arm64
# SDL2 instead of an amd64 one. The CMake config package is the fallback.
set(LUXOR_HAVE_SDL2 FALSE)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    if(SDL2_FOUND)
        add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
        set(LUXOR_HAVE_SDL2 TRUE)
    endif()
endif()
if(NOT LUXOR_HAVE_SDL2)
    find_package(SDL2 QUIET CONFIG)
    if(SDL2_FOUND)
        set(LUXOR_HAVE_SDL2 TRUE)
    endif()
endif()

if(NOT LUXOR_HAVE_SDL2)
    message(STATUS "SDL2 not found: building the headless core only")
    return()
endif()

find_library(GLESV2_LIBRARY NAMES GLESv2)

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
# libGLESv2 even though it does not create its own GL renderer.
add_executable(device_probe device_probe.c)
if(GLESV2_LIBRARY)
    target_link_libraries(device_probe PRIVATE SDL2::SDL2 ${GLESV2_LIBRARY})
else()
    target_link_libraries(device_probe PRIVATE SDL2::SDL2)
endif()
