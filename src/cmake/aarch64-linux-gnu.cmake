# Cross-compile toolchain for the RG40XXV (Linux ARM64, muOS).
#
#   cmake -S . -B build-aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
#
# The device ABI was verified on hardware: `uname -m` reports aarch64
# (docs/DEVICE_REPORT.md). SDL2 is linked dynamically against the image's
# SDL2 2.28.5, so point CMAKE_PREFIX_PATH at the arm64 development files when
# using distro packages:
#
#   sudo dpkg --add-architecture arm64
#   echo 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy main universe' \
#     | sudo tee /etc/apt/sources.list.d/ports-arm64.list
#   sudo apt-get update && sudo apt-get install \
#     libsdl2-dev:arm64
#   cmake -S . -B build-aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
#     -DCMAKE_PREFIX_PATH=/usr/lib/aarch64-linux-gnu

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TOOLCHAIN_PREFIX aarch64-linux-gnu-)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}ranlib)
set(CMAKE_STRIP        ${TOOLCHAIN_PREFIX}strip)

# Debian/Ubuntu arm64 packages land in /usr/lib/aarch64-linux-gnu, not
# /usr/aarch64-linux-gnu. Root BOTH so find_package/find_library reach them.
# Override with -DCMAKE_FIND_ROOT_PATH=... to point at a custom sysroot.
if(NOT CMAKE_FIND_ROOT_PATH)
    set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Debian/Ubuntu multiarch headers (e.g. SDL2/_real_SDL_config.h) live in
# /usr/include/aarch64-linux-gnu, which is NOT in the cross compiler's
# default include path. GCC rewrites -I paths with the sysroot prefix, so
# this stays correct when CMAKE_SYSROOT points at a custom sysroot.
if(NOT CMAKE_SYSROOT)
    set(CMAKE_SYSROOT "/")
endif()
set(CMAKE_C_FLAGS_INIT "-I/usr/include/aarch64-linux-gnu")
set(CMAKE_CXX_FLAGS_INIT "-I/usr/include/aarch64-linux-gnu")

# The Ubuntu SDL2 shared library carries DT_NEEDED entries for optional
# X11/Wayland/audio backends. Tell the cross-linker where the multiarch
# dependency DSOs live, without adding them to the executable's link list.
# libpulse lives in a subdirectory that must be listed separately.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wl,-rpath-link,/usr/lib/aarch64-linux-gnu \
     -Wl,-rpath-link,/usr/lib/aarch64-linux-gnu/pulseaudio \
     -Wl,-rpath-link,/lib/aarch64-linux-gnu")

# Cross-compiling SDL2 built from source is possible but unnecessary: the
# image ships SDL2 2.28.5 and linking dynamically keeps the muOS video driver
# and joystick integration intact (docs/DEVICE_REPORT.md). If you do build
# SDL2 yourself, configure it with these options:
#   -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_VIDEO_OPENGLES=ON
#   -DSDL_VIDEO_X11=OFF -DSDL_VIDEO_WAYLAND=OFF -DSDL_AUDIO=OFF
