# Device report: RG40XXV / MustardOS 2601.0

Source: user-uploaded `device_report.txt` collected from the target on 2026-10-26.

## Verified facts

| Item | Result |
|---|---|
| uname | `Linux muos-7d2552 4.9.170 ... aarch64 muOS/2 NeXT` |
| Architecture | `aarch64`, 64-bit |
| OS | MustardOS 2601.0 (JACARANDA), build `bc38efa0` |
| SDL2 | `/usr/lib/libSDL2-2.0.so.0.2800.5` → SDL2 **2.28.5** |
| SDL2_image | `/usr/lib/libSDL2_image-2.0.so.0.800.2` |
| SDL2_mixer | `/opt/zulu/lib/libSDL2_mixer-2.0.so.0.600.3` |
| SDL2_ttf | `/usr/lib/libSDL2_ttf-2.0.so.0.2200.0` |
| SDL2_net | `/usr/lib/libSDL2_net-2.0.so.0.200.0` |
| SDL2_gfx | `/usr/lib/libSDL2_gfx-1.0.so.0.0.2` |
| EGL | `/usr/lib/libEGL.so.1` |
| GLES2 | `/usr/lib/libGLESv2.so.2` |
| GLES1 | `/usr/lib/libGLESv1_CM.so.1` |
| Desktop GL | `/usr/lib/libGL.so.1` |
| DRM node | **None listed** (`/dev/dri` directory not present) |
| Framebuffer | `/dev/fb0` (major 29, minor 0) |
| Input | `/dev/input/js0`, `event0`, `event1`, `event2` |
| PortMaster libs | `LD_LIBRARY_PATH=/opt/muos/frontend/lib` |

## Implications

- Build **aarch64**. Use `aarch64-linux-gnu-` toolchain.
- The system SDL2 is modern enough; dynamically link it. Do not static-link SDL2.
- The device has **no DRM render node**, so SDL KMSDRM cannot run. `/dev/fb0`
  exists. Try SDL video drivers `x11`, `wayland`, and `fbdev` on device.
- Even though EGL/GLES2 libraries exist, file presence is not proof that SDL can
  create a GLES2 window/context with the active video driver.
- A native `SDL_Renderer` 2D backend is included and is the recommended safe
  path for the device; `luxor3_gl` tries GLES2 and can fall back to 2D.
- Add `/opt/zulu/lib` to `LD_LIBRARY_PATH` if using SDL2_mixer.

## Still needs a runtime probe

The uploaded report lists files only. It does not contain results for
`SDL_GetCurrentVideoDriver()`, `SDL_GL_CreateContext()`, `GL_RENDERER`, joystick
mapping, or touch devices. Build `src/device_probe` on an aarch64 sysroot and
run it on device as described in `docs/DEVICE_REPORT.md`.
