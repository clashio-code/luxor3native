# Checklist verifikasi RG40XXV + muOS

**Tujuan:** tentukan arsitektur userspace, SDL2, EGL/GLES2, input, dan package
layout dari *image yang benar-benar terinstall*. Jangan asumsikan ABI dari
spesifikasi handheld — muOS image perlu diukur langsung.

## Yang sudah diverifikasi dari dokumentasi

- [x] RG40XX V tercantum di supported devices muOS.
- [x] Resolusi layar **640x480**, 1 analog stick.
- [x] muOS 2606.0 Andromeda ada dalam channel release saat ini.
- [x] PortMaster convention: `/ports/<port>/` + launch entry di `/roms/Ports/` +
      shared libs di `MUOS/PortMaster/libs`.

Sumber: [Supported Devices](https://muos.dev/devices),
[PortMaster guide](https://muos.dev/installation/portmaster),
[Current release](https://muos.dev/release/current/2606_0).

## A. Lakukan di RG40XXV (terminal/SSH)

Boot ke muOS, buka terminal (atau SSH jika sudah diaktifkan), jalankan satu
script berikut. Output disimpan ke `device_report.txt` pada cwd. **Script hanya
baca sistem**, tidak mengubah config.

```sh
#!/bin/sh
{
  echo "=== DEVICE ==="
  date -Iseconds 2>/dev/null || date
  uname -a
  echo "--- architecture ---"
  uname -m
  getconf LONG_BIT 2>/dev/null || true
  echo "--- OS ---"
  cat /etc/os-release 2>/dev/null || true
  echo "--- CPU ---"
  cat /proc/cpuinfo 2>/dev/null | head -60
  echo "--- SDL2 libs ---"
  find /usr/lib /lib /opt /mnt/SDCARD/MUOS -iname '*SDL2*' -o -iname 'libSDL*' 2>/dev/null | sort
  echo "--- EGL / GLES libs ---"
  find /usr/lib /lib /opt /mnt/SDCARD/MUOS \( -iname '*GLES*' -o -iname '*EGL*' \) 2>/dev/null | sort
  echo "--- GL/render nodes ---"
  ls -l /dev/dri /dev/fb* /dev/input 2>/dev/null || true
  echo "--- PortMaster libs ---"
  find /mnt/SDCARD/MUOS/PortMaster /mnt/SDCARD/ports -maxdepth 4 -type f \
       \( -iname '*SDL2*' -o -iname '*GLES*' -o -iname '*EGL*' \) 2>/dev/null | sort
  echo "--- video env ---"
  env | sort | grep -E 'SDL|EGL|GLES|DRM|FB|LD_LIBRARY_PATH' || true
} > device_report.txt 2>&1
cat device_report.txt
```

> If `find` complains about a path that does not exist, that is fine.
> muOS SD card label/path can vary — adjust `/mnt/SDCARD` to the actual mount.

## B. Determine SDL2 capabilities

From B.1, pick up the exact runtime library path, copy the library (or use the
PortMaster squashfs if the system libs are not directly accessible) and run on
host `file`, `readelf`, and `strings`. Or compile/run a small native probe after
establishing architecture in section C.

A runtime probe should report:

- `SDL_GetVersion()` → version fields.
- `SDL_GetCurrentVideoDriver()` after init → driver name (`kmsdrm`, `wayland`,
  `x11`, `dummy`, etc.).
- `SDL_GL_CreateContext()` with `SDL_GL_CONTEXT_PROFILE_ES`, major 2, minor 0.
- `glGetString(GL_VERSION)`, `GL_RENDERER`, `GL_VENDOR`, `GL_EXTENSIONS`.
- `SDL_GetNumTouchDevices()`, `SDL_NumJoysticks()`, joystick name and
  `SDL_GameControllerMapping()`.

**Critical:** `SDL_VIDEODRIVER=info` is not a portable SDL2 query and may just
fail. Instead query driver in the app with `SDL_GetCurrentVideoDriver()` after
`SDL_Init(SDL_INIT_VIDEO)`. To enumerate possible drivers, inspect SDL2 `strings`
for `kmsdrm`, `x11`, `wayland`, `fbcon` — this only tells compiled-in support,
not whether SDL can create a GLES context on device.

## C. Pick build target

| `uname -m` | Build target | Compiler prefix |
|---|---|---|
| `aarch64` | AArch64 ELF64 | `aarch64-linux-gnu-` |
| `armv7l` | ARM hard-float ELF32 | `arm-linux-gnueabihf-` |

Do **not** link to a library with the wrong ELF class/architecture:

```sh
file libSDL2-2.0.so.0 libGLESv2.so.2 libEGL.so.1
readelf -h libSDL2-2.0.so.0 | grep -E 'Class:|Machine:'
```

If system SDL2 version/ABI unknown, prefer one of:

1. Link against the **PortMaster runtime** delivered for this exact device and
   declare its package dependency; or
2. Static-link SDL2 in CI **only if its video backend and EGL/GLES route are
   verified** against the device.

Static SDL2 does **not** solve a missing EGL/GLES driver.

## D. Input check (no dependency on touch)

RG40XXV is a physical handheld; design controls assuming **D-pad + buttons +
one analog stick**. Touch is unverified; don't make swipe mandatory.

Recommended gameplay mapping:

| Input | Action |
|---|---|
| Left stick X / D-pad left-right | aim shooter (analogue for stick, discrete/held for D-pad) |
| A / south face button | fire |
| B / east face button | swap ammo |
| L / R | alternate aim / swap (configurable) |
| Start | pause/menu |
| Select | screenshot or secondary utility; not a gameplay dependency |
| touch (if SDL reports it) | optional absolute pointer/aim, fire-on-tap, never required |

Probe actual SDL joystick button/axis indices; do not assume gamecontroller
mapping exists. Ship a remappable `input.cfg` with defaults.

## E. Display and port package

- Render into **logical 800x600** (native game art resolution).
- RG40XXV panel **640x480** has same 4:3 aspect, so uniform 0.8× downscale —
  no letterboxing needed.
- Keep all original artwork in one `data/` sibling folder, not in executable.
- PortMaster package: unique lowercase ID such as `luxor3-native`; no writes into
  another port's directory. Save user settings/progress separately under the
  muOS save path as discovered from an existing port package.
- Do not bundle `Luxor3.exe`, `fmod.dll` or extracted assets in a public repo.

## F. Report back

Copy back these lines from `device_report.txt`:

1. `uname -m` and `getconf LONG_BIT`
2. SDL2 library path and `SDL_GetVersion()`
3. `SDL_GetCurrentVideoDriver()` result
4. EGL/GLES library paths + `GL_RENDERER`/`GL_VERSION`
5. `SDL_NumJoysticks`/device name and `SDL_GetNumTouchDevices`

Until those values are supplied, target ABI / SDL2 / GLES2 support is **not
verified**, and a CI build cannot be called compatible with the RG40XXV image.
