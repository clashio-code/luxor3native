# Build

## Current working build

The portable core compiles with GCC or Clang and has no SDL2/GLES dependency.
That means asset/track/game logic is buildable and testable on CI even before
we know the target device's exact SDL2 image.

```sh
cd src
make -j4
./luxor3_test "/path/to/Luxor 3"
./luxor3_preview "/path/to/Luxor 3" \
  "/path/to/Luxor 3/data/levels/classic/A_1_1.lvl" preview.png
```

`luxor3_preview` renders to a 800x600 PNG using the same `Scene` built by the
native renderer; it does not require a GPU. It is a host-side visualization
and is not shipped on the SD card.

## SDL2 device target

The device image ships SDL2 **2.28.5** at `/usr/lib/libSDL2-2.0.so.0.2800.5`
plus `libSDL2_image`, `libSDL2_mixer`, `libSDL2_ttf`, `libSDL2_net` and
`libSDL2_gfx`. Link SDL2 dynamically — static-linking it would break the
video-driver and joystick integration with muOS.

The device has **no `/dev/dri`**, so the video driver (and therefore whether a
GLES2 context can be created) is not known until runtime. Two binaries are
built:

| Binary | Backends | Notes |
|---|---|---|
| `luxor3` | `SDL_Renderer` 2D | Works on every driver. Safe default here. |
| `luxor3_gl` | GLES2 + fallback to 2D | Tries GLES2 first, falls back automatically. |

Runtime selection via `LUXOR_RENDERER`:

```sh
LUXOR_RENDERER=gl   # force GLES2 (exit if it fails)
LUXOR_RENDERER=2d   # force the 2D backend
# unset              # try GL, fall back to 2D (default)
```

```sh
# Cross-build prerequisites on an Ubuntu/Debian host:
sudo dpkg --add-architecture arm64
sudo apt-get install -y \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake ninja-build pkg-config
# The arm64 SDL2 development package comes from the Ubuntu ports archive.
# Jammy (22.04) is used deliberately: its SDL2 does not require the newer
# GLIBC 2.38 symbols that noble's SDL2 would pull in.
echo 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy main universe' \
  | sudo tee /etc/apt/sources.list.d/ports-arm64.list
sudo apt-get update
sudo apt-get install -y libsdl2-dev:arm64

cd src
cmake -S . -B build-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/lib/aarch64-linux-gnu
cmake --build build-aarch64 -j"$(nproc)"

# Verify output architecture:
file build-aarch64/luxor3
readelf -h build-aarch64/luxor3 | grep -E 'Class:|Machine:'
```

The toolchain file already handles the multiarch details: the
`/usr/include/aarch64-linux-gnu` header directory and the `-rpath-link` entries
the cross-linker needs for SDL2's optional X11/Wayland/PulseAudio
dependencies.

Only `libsdl2-dev:arm64` is needed. The GLES2 headers are vendored in
`third_party/khronos`, and `libGLESv2.so.2` is present on the device itself, so
the GL backend links against the runtime library without a separate dev
package.

The cross toolchain is `aarch64-linux-gnu-` — confirmed by `uname -m` on the
device (`aarch64`). No 32-bit path is needed.

### Launcher on the device

`libSDL2_mixer` lives in `/opt/zulu/lib`, not `/usr/lib`, so the launcher must
extend the library path:

```sh
#!/bin/sh
PORT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$PORT_DIR:/opt/zulu/lib:$LD_LIBRARY_PATH"
exec "$PORT_DIR/luxor3" "$PORT_DIR/data"
```

## CI / headless test

```sh
cd src
make -j4
./luxor3_test "$GAME_ROOT"
./luxor3_preview "$GAME_ROOT" \
  "$GAME_ROOT/data/levels/classic/A_1_1.lvl" preview.png
```

No copyrighted assets are in this repository. CI must receive game data only
from a private/local source; see `docs/ASSET_POLICY.md`.

## SD-card folder layout

Use a unique folder and launcher; never overwrite another port:

```text
/mnt/SDCARD/Ports/Luxor3/
  luxor3       # native ARM Linux executable
  data/        # user-supplied game data, local/private
  luxor3.sh    # PortMaster/muOS launcher
```
