# Hasil probe perangkat — RG40XXV / MustardOS

Sumber: `device_report.txt` dari perangkat (2026-10-26). Rekap di
`tools/device_report.md`.

## Ringkas

| Item | Nilai | Status |
|---|---|---|
| Arsitektur | `aarch64`, 64-bit | ✅ toolchain `aarch64-linux-gnu-` |
| OS | MustardOS 2601.0 "JACARANDA", build `bc38efa0` | ✅ |
| Kernel | 4.9.170 (Dec 2025) | ✅ |
| SDL2 | `/usr/lib/libSDL2-2.0.so.0.2800.5` | ✅ **SDL2 2.28.5** (libtool soname encodes binary age) |
| SDL2_image | `/usr/lib/libSDL2_image-2.0.so.0.800.2` | ✅ |
| SDL2_mixer | `/opt/zulu/lib/libSDL2_mixer-2.0.so.0` | ✅ (path berbeda) |
| SDL2_ttf | `/usr/lib/libSDL2_ttf-2.0.so.0` | ✅ |
| SDL2_net | `/usr/lib/libSDL2_net-2.0.so.0` | ✅ |
| SDL2_gfx | `/usr/lib/libSDL2_gfx-1.0.so.0` | ✅ |
| GLES2 | `/usr/lib/libGLESv2.so.2` | ✅ |
| EGL | `/usr/lib/libEGL.so.1` | ✅ |
| GLESv1_CM | `/usr/lib/libGLESv1_CM.so.1` | ✅ |
| Desktop GL | `/usr/lib/libGL.so.1` | ✅ |
| DRM | **`/dev/dri` tidak ada** | ⚠️ lihat bawah |
| Framebuffer | `/dev/fb0` (major 29, minor 0) | ✅ |
| Input | `/dev/input/js0`, `event0..2` | ✅ |
| PortMaster libs | `/opt/muos/frontend/lib` | ✅ |

## Koreksi terhadap ekspektasi sebelumnya

1. **Versi muOS**: riset web menyebut "2606.0 Andromeda"; device ini
   menjalankan **2601.0 JACARANDA**. Yang terpasang lebih lawas.
2. **SDL2 ada, dan versinya modern** — `libSDL2-2.0.so.0.2800.5` → SDL2
   **2.28.5**. Angka `2800` itu *binary age* dari libtool, bukan nomor build:
   SDL2 menghitung `SDL_BINARY_AGE = minor*100 + micro` dan
   `INTERFACE_AGE = micro` untuk branch stabil, jadi soname `0.2800.5`
   = minor 28, micro 5. Semua API yang kita pakai sudah ada.
3. **`/dev/dri` tidak ada.** Ini penting.Dugaan awal bahwa `/dev/dri/` ada
   adalah salah baca: angka `29, 0` di listing adalah major/minor device
   milik `/dev/fb0`, bukan jumlah node DRM. Sections `/dev/dri` juga tidak
   muncul di output. Jadi:
   - **KMSDRM tidak bisa dipakai** (tidak ada DRM render node).
   - SDL2 harus lewat **framebuffer** (`fbdev`) atau **X11**/**Wayland**.
   - Tapi `/dev/fb0` **ada** → `fbdev` adalah jalur yang realistis.
   - `libGLESv2.so.2` + `libEGL.so.1` ada, jadi software GLES2 (Mesa) atau
     EGL-over-fbdev mungkin tersedia.
4. **`libSDL2_mixer` ada di `/opt/zulu/lib`**, bukan `/usr/lib` → launcher harus
   menambah `/opt/zulu/lib` ke `LD_LIBRARY_PATH`.
5. **GLESv1_CM juga ada** — jalan felempar untuk engine lama.

## Konsekuensi ke strategi build

### aarch64 confirmed
Target binary: **ARM64 ELF**, compiler `aarch64-linux-gnu-`. Tidak perlu
jalur 32-bit sama sekali.

### Pakai SDL2 sistem, jangan static-link
Versi 2.28.x sudah menyediakan window, GLES2 context, gamecontroller,
image, mixer, dan gfx. Static-link justru memutus integrasi video driver
dan joystick muOS. Link dinamis:

```
-lSDL2 -lSDL2_image -lSDL2_mixer -lGLESv2 -lEGL
```

### Prioritas: tentukan video driver
Karena tidak ada DRM node, urutan cobaan di perangkat:

1. `SDL_VIDEODRIVER=x11` — kalau muOS menjalankan X server (port emulator
   mungkin butuh X, jadi kemungkinan ada).
2. `SDL_VIDEODRIVER=wayland` — kalau compositor Wayland aktif.
3. `SDL_VIDEODRIVER=fbdev` — selalu tersedia karena `/dev/fb0` ada, tapi
   SDL2 2.28 **tidak** punya backend fbdev yang support OpenGL. fbdev =
   framebuffer murni, tanpa GL.
4. `SDL_VIDEODRIVER=dummy` — hanya untuk test headless.

**Konsekuensi penting:** kalau ternyata hanya `fbdev` yang jalan, GLES2 via
SDL2 tidak tersedia. Jalan keluar:

- **Software GLES2** (Mesa llvmpipe/swrast via EGL surfaceless) → tetap
  GLES2 API, tapi lambat. Untuk game 2D sprite_batch yang mungkin masih
  cukup, tapi perlu benchmark.
- **Renderer 2D via SDL2_Renderer** (blit ke framebuffer) → jauh lebih
  sederhana dan **kemungkinan besar lebih cepat** untuk 2D sprite daripada
  GLES2 di CPU kecil ini. `SDL2_gfx` juga tersedia.

Rekomendasi: **implementasikan renderer 2D berbasis `SDL_Renderer` sebagai
fallback**, bukan cuma GLES2.-core-nya (scene, path, aset) sudah
backend-agnostic, jadi ini murah.

## Probe runtime berikutnya (wajib)

Jawaban di atas masih berbasis *listing file*. Yang belum diketahui dan
menentukan arsitektur renderer:

```sh
cat > probe.c <<'EOF'
#include <SDL2/SDL.h>
#include <stdio.h>
int main(void) {
    SDL_version_t v; SDL_GetVersion(&v);
    printf("linked SDL: %d.%d.%d\n", v.major, v.minor, v.patch);
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError()); return 1;
    }
    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());
    int n = SDL_GetNumVideoDrivers();
    printf("compiled drivers:");
    for (int i = 0; i < n; ++i) printf(" %s", SDL_GetVideoDriver(i));
    printf("\n");
    printf("joysticks: %d\n", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        printf("  [%d] %s (gc=%d)\n", i, SDL_JoystickNameForIndex(i),
               SDL_IsGameController(i));
    printf("touch devices: %d\n", SDL_GetNumTouchDevices());
    SDL_Quit();
    return 0;
}
EOF
gcc probe.c -o probe $(pkg-config --cflags --libs sdl2) && ./probe

# lalu coba tiap driver
for d in x11 wayland kmsdrm fbdev dummy; do
  echo "== $d =="; SDL_VIDEODRIVER=$d ./probe 2>&1 | head -4
done
```

Dan probe GLES2 (hanya kalau ada driver yang bisa window+GL):

```sh
cat > probe_gl.c <<'EOF'
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include <stdio.h>
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    printf("driver=%s\n", SDL_GetCurrentVideoDriver());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_Window*w=SDL_CreateWindow("p",0,0,640,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!w){printf("window failed: %s\n",SDL_GetError());return 1;}
    SDL_GLContext c=SDL_GL_CreateContext(w);
    if(!c){printf("ctx failed: %s\n",SDL_GetError());return 1;}
    printf("VENDOR=%s\nRENDERER=%s\nVERSION=%s\n",
      (const char*)glGetString(GL_VENDOR),(const char*)glGetString(GL_RENDERER),
      (const char*)glGetString(GL_VERSION));
    int w2,h2; SDL_GL_GetDrawableSize(w,&w2,&h2); printf("drawable=%dx%d\n",w2,h2);
    return 0;
}
EOF
gcc probe_gl.c -o probe_gl $(pkg-config --cflags --libs sdl2) -lGLESv2 && ./probe_gl
```

## Yang harus dijawab sebelum lanjut coding renderer

1. Video driver mana yang berhasil?
2. GLES2 context berhasil dibuat? Kalau ya, `GL_RENDERER` apa?
3. Joystick: namanya apa, ada gamecontroller mapping?
4. Touchscreen ada?

Jawaban 1–2 menentukan apakah kita pakai GLES2 atau `SDL_Renderer` 2D.
