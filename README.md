# Luxor3 Native — PC-to-RG40XXV port workspace

Native reimplementation target: C/C++ + SDL2 + OpenGL ES 2.0, built on PC/CI
for muOS on Anbernic RG40XXV. The original `Luxor3.exe` is never launched;
no Box86, Box64, Wine, or Proton.

## Status

See [`docs/FINDINGS.md`](docs/FINDINGS.md) for verified answers to the four
initial questions, [`docs/FORMAT_SPEC.md`](docs/FORMAT_SPEC.md) for the
reverse-engineered text data formats, and [`docs/COORDINATES.md`](docs/COORDINATES.md)
for the coordinate system and the Linux path gotchas.

**Device facts confirmed** (`docs/DEVICE_REPORT.md`): aarch64, MustardOS
2601.0, SDL2 2.28.5, `libGLESv2.so.2` + `libEGL.so.1` present, but **no
`/dev/dri`** — so the video driver decides at runtime whether GLES2 is
possible. The port ships an `SDL_Renderer` 2D backend for that case.

**Implemented and verified on a build host (no device needed):**

- Case-insensitive asset resolution (`PathTable`) — the shipped data references
  202 files with different capitalisation than on disk, which would fail on a
  case-sensitive Linux filesystem.
- `.lvl` / `.LX3TRK` parsing, track geometry, arc-length parameterisation,
  junction vertex reuse.
- World-to-screen coordinate conversion (`screen_y = -world_y`), verified
  against four map backgrounds.
- PNG decode, sprite rasterisation, batched GLES2 quad renderer.
- Offline preview renderer that produces the same scene as the GL backend
  without a GPU, so CI can assert on rendered output.
- 43 headless assertions in `src/test_core.cpp`, all passing.
- Two renderer backends sharing one scene: GLES2 (`src/sprite_batch.cpp`) and
  `SDL_Renderer` 2D (`src/sdl2d.cpp`), with automatic GL-to-2D fallback.
- The 2D backend was smoke-tested end to end (window, texture upload, scene,
  frame loop) with `SDL_VIDEODRIVER=dummy`.
- All 38 GL symbols used are core GLES2; no desktop-only entry points.

- Original Windows executable is **PE32 / x86 32-bit**, timestamp 2007-10-05.
- It uses x87 floating point heavily, FMOD 3 for sound, and OpenGL 1.x
  fixed-function rendering (plus an unused D3D8 import).
- The game data tree is **3,709 files / 51.8 MiB**: PNG/JPG sprites, OGG audio,
  text `.lvl` levels, `.LX3TRK` track geometry, and `.effect` particle definitions.
- All 223 levels parse; all 374 includes resolve; 363 track references resolve;
  asset reference audit reports 0 missing references.
- RG40XXV LCD is **640x480**. Exact muOS ABI, SDL2 version/video backend, EGL/GLES
  and input capabilities are **not yet confirmed on the device**.

## Tools

All tools are safe to run from a host PC. They read the original installation;
they do not modify or execute the Windows binary.

```sh
python3 tools/pe_probe.py "/path/to/Luxor 3/Luxor3.exe"
python3 tools/asset_inventory.py "/path/to/Luxor 3"
python3 tools/asset_audit.py "/path/to/Luxor 3"
python3 tools/luxor_parse.py track "/path/to/Luxor 3/data/maps/geb/path.LX3TRK"
python3 tools/luxor_parse.py level "/path/to/Luxor 3/data/levels/classic/A_1_1.lvl"
python3 tools/luxor_parse.py validate "/path/to/Luxor 3"
```

See [`docs/DEVICE_CHECKLIST.md`](docs/DEVICE_CHECKLIST.md) for the on-device
muOS/SDL2/EGL/GLES probe. See [`docs/ASSET_POLICY.md`](docs/ASSET_POLICY.md)
for copyright-safe repo and packaging rules.

## Building and testing

```sh
cd src
make -j4
./luxor3_test "/path/to/Luxor 3"                       # headless assertions
./luxor3_preview "/path/to/Luxor 3" \
  "/path/to/Luxor 3/data/levels/classic/A_1_1.lvl" preview.png
make luxor3                                          # 2D backend
make luxor3_gl                                       # 2D + GLES2 backend
make device_probe                                    # runtime probe for the device
```

Run the device probe on the RG40XXV to learn the video driver and whether a
GLES2 context can be created:

```sh
cd src && make device_probe
scp device_probe <device>:/tmp/ && ssh <device> /tmp/device_probe
for d in x11 wayland kmsdrm fbdev dummy; do
  ssh <device> "SDL_VIDEODRIVER=$d /tmp/device_probe"
done
```

The core builds without SDL2 or GLES2, so CI can verify game logic before the
device's SDL2 version is known. See [`BUILD.md`](BUILD.md).

## Continuous integration

`.github/workflows/ci.yml` runs three jobs on every push and pull request, and
can also be started manually from **Actions → CI → Run workflow**:

| Job | What it does |
|---|---|
| `core` | Builds the SDL-free core with `-Werror`, runs the headless assertions against the synthetic fixture, renders a fixture preview PNG and checks it is 800x600, regenerates the fixture to prove it is reproducible, and fails if any copyrighted asset is tracked. |
| `python-tools` | Compile-checks and exercises the data-analysis tools against the fixture. |
| `cross-build` | Cross-compiles `luxor3`, `luxor3_gl` and `device_probe` for AArch64 against `libsdl2-dev:arm64`, verifies the ELF machine type, and uploads the ARM64 binaries as a downloadable artifact. |

No job touches the copyrighted game data: they all use
`tests/fixtures/mini_game`, a synthetic fixture generated by
`tools/make_fixture.py`.

## Port architecture

- Native C++ executable `luxor3` + independent `data/` sibling folder.
- Build on PC/CI with an ARM64 Linux cross-toolchain; target device libs must
  be verified before calling a build compatible.
- Logical render size: 800x600; downscale uniformly to 640x480 (both 4:3).
- Suggested asset loaders: `stb_image` for PNG, `stb_vorbis` or SDL_mixer for
  OGG, custom bitmap-font loader for `.fontdef` atlas.
- Suggested GLES2 renderer: batched textured quads, alpha blend, additive
  particle pass. Preserve original art dimensions and per-level geometry.
- Port package stays separate, e.g. `Ports/Luxor3/`; never overwrites an
  existing port.

## Asset policy

Game assets are copyrighted. Do not add the extracted `data/` folder or any
original executable/DLL to a public repository or public release. The user
should supply data from their own game installation locally. See
[`docs/ASSET_POLICY.md`](docs/ASSET_POLICY.md).
