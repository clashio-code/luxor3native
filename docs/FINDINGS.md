# Laporan Temuan — Luxor 3 → RG40XXV (muOS)

Tanggal investigasi: 2026-10-26. Semua temuan di bawah diverifikasi dengan alat
di `tools/` terhadap salinan data game yang ada di mesin ini.

---

## Pertanyaan 1 — `Luxor3.exe` 32-bit atau 64-bit?

**32-bit x86 (PE32, I386).** Bukti dari `tools/pe_probe.py`:

```
file        : Luxor3.exe
size        : 2097152 bytes (2.00 MiB)
sha256      : 2be52aa5447de75a5d3a944d42ccaabcb9c46a2335de65acce88e24cb0071c45
machine     : 0x014c  I386 (x86, 32-bit)
timestamp   : 2007-10-05T16:51:55Z
opt magic   : 0x10b  PE32 (32-bit)
linker      : 8.0
entry rva   : 0xd306c   imagebase 0x400000
subsystem   : 2 WINDOWS_GUI
dll chars   : 0x0 (none)      <- tanpa ASLR/NX
```

### Implications untuk port

**Tidak ada jalur 32-bit yang perlu dipertahankan.** Batasan "native ARM64"
dengan sendirinya sudah menghapus kebutuhan kompatibilitas x86; tidak ada
Box86/Box64/Wine/Proton yang boleh dipakai. Yang penting dari temuan ini:

- **x87 (LSU) dominan.** Disassembly `objdump` atas 1.7 MiB `.text` memberi
  ~11.400 instruksi x87 (`fld`/`fstp`/`fmul`/`fadd`/`fdiv`/`fild`/`fcomp` …)
  terhadap ~1.600 instruksi SSE. Artinya float game asli dihitung dalam
  **extended precision 80-bit** (x87), bukan float32 IEEE.
- **Konsekuensi:** kalau port memakai `float` (float32) untuk simulasi train/bola,
  hasil bisa **berbeda** dari game asli pada akumulasi panjang — kecepatan train,
  posisi bola, dan ambang collision bisa bergeser sedikit. Rekomendasi:
  pakai `double` (float64) di port, atau `float` dengan urutan operasi yang
  sama seperti x87, lalu bandingkan angka kunci (posisi bola pada frame N,
  skor akhir) terhadap video referensi PC.
- Binary asli butuh `fmod.dll` (FMOD 3) untuk audio dan `OPENGL32.dll`
  (OpenGL 1.x **fixed-function**) untuk render — bukan shader. Lihat
  `docs/FORMAT_SPEC.md` §8 dan daftar import lengkap di bawah.

### Import table (ringkas, 210 fungsi)

| DLL | Jumlah | Catatan penting |
|---|---|---|
| `KERNEL32.dll` | 103 | standar |
| `USER32.dll` | 29 | window, input, cursor |
| `OPENGL32.dll` | 44 | **fixed-function GL 1.x**: `glBegin/glEnd`, `glVertex2f`, `glOrtho`, `glPushMatrix`, `glTexImage2D`, `glDrawArrays`. **Tidak ada** `glCreateShader`/GLSL. |
| `fmod.dll` | 27 | FMOD 3: `_FSOUND_Init`, `_FSOUND_Stream_Open`, ... |
| `GDI32.dll` | 3 | `SetPixelFormat`, `ChoosePixelFormat`, `SwapBuffers` |
| `ADVAPI32.dll` | 3 | registry |
| `d3d8.dll` | 1 | `Direct3DCreate8` — **tidak dipakai** (tersisa dari percabangan renderer) |

Artinya: renderer asli = OpenGL 1.x 2D immediate mode. Port dengan GLES 2.0
memakai shader → bersih, dan mapping dari `glOrtho/glBegin/glEnd` ke
`glVertexAttrib` + shader sprite straightforward.

Ada juga **Authenticode signature** (4.096 byte overlay `PK…`) di akhir file.

---

## Pertanyaan 2 — Isi folder `data/`

`data.zip` sudah diekstrak. Inventarisasi lengkap (`tools/asset_inventory.py`):

```
files: 3709   total: 51.8 MiB

by extension:
  .png         2739     25356.6 KiB
  .effect      264       970.6 KiB
  .txt         239       723.8 KiB
  .lvl         223       127.2 KiB
  .ogg         108      13147.5 KiB
  .jpg         100      9025.7 KiB
  .lx3trk       29       339.4 KiB
  .fontdef       1        19.8 KiB
```

### Ekstensi yang ada vs yang kamu kira ada

| Dugaan awal | Status | Sebenarnya |
|---|---|---|
| `.png` | ✅ | 2.739 file, dominan (sprite + animasi) |
| `.jpg` | ✅ | 100 file (background map 800x600 + thumbnail 151x108) |
| `.tga` | ❌ | **tidak ada** |
| `.wav` | ❌ | **tidak ada** — audio seluruhnya `.ogg` |
| `.ogg` | ✅ | 108 file (music 5, fx 95, ui 8) |
| `.lvl` | ✅ | 223 file |
| `.dat` | ⚠️ | hanya 1: `unins000.dat` (file uninstaller Inno Setup, bukan data game) |
| `.xml` | ❌ | **tidak ada** — semua config berformat teks kustom |
| `.cfg` | ❌ | **tidak ada** — konfigurasi pakai `.txt` |
| tambahan | — | `.effect` (264, partikel), `.LX3TRK` (29, lintasan), `.fontdef` (1, bitmap font) |

### Sistem koordinat & viewport

- **Viewport asli 800x600** (semua background map dan mask tepat 800x600).
- Koordinat track (`path.LX3TRK`) memakai **pixel dengan Y terbalik** (negatif =
  bawah). Rentang x ∈ [-36, 840], y ∈ [-486, +40].
- Artwork bola 32x32; sprite UI ukuran beragam.
- `#include` **ada dan dipakai luas**: 374 include di 223 `.lvl`, resolusi
  relatif ke folder file peng-include, **0 rusak**.

Detail grammar & contoh lengkap: `docs/FORMAT_SPEC.md`.

### Apakah level pakai `#include`?

**Ya.** Empat pola include di level:

| Include | Isi | Dipakai oleh |
|---|---|---|
| `flavors_1..4.txt` | `AllowableGamePieceFlavors` per difficulty | classic, centipede, treasureHunter |
| `treasure_1..11.txt` | `AllowableTreasureFlavors` (Gem01.., Ring01) | semua mode |
| `survivalData.txt` | `TrainUpgradeData` per difficulty (tambah warna per train ke-N) | survival |
| `powerups.txt` | daftar powerup aktif | crossingTheNile |

Tingkat resolution aset:

```
include refs        : 374  (0 unresolved)
animation refs      : 97   (0 unresolved; 94 = frame family)
.effect texture refs: 960  (0 unresolved)
placeable effect refs:157 (0 unresolved)
UI texture refs     : 521  (0 unresolved)
GameSounds refs     : 101  (0 unresolved)
locale key refs     : 643  (0 unresolved; English.txt punya 662 key)
TrackSection refs   : 363  (0 unresolved, terhadap 28 map .LX3TRK)
```

---

## Pertanyaan 3 — Runtime RG40XXV

**Status: belum terverifikasi dari image muOS yang konkret.** Yang bisa
dikonfirmasi dari sumber publik dan apa yang masih harus diukur di perangkat.

### Yang terverifikasi

- **Resolusi layar 640x480** — muOS mendaftarkan RG40XX V dengan layar
  640x480 ([muOS Supported Devices](https://muos.dev/devices)).
- **Konvensi paket port** — muOS memakai pola PortMaster: data port di
  `/ports/<nama>/`, skrip launch di `/roms/Ports/`, runtime library bersama di
  `MUOS/PortMaster/libs` ([muOS PortMaster guide](https://muos.dev/installation/portmaster)).
  Artinya port Luxor3ELD harus dilation sebagai **folder port terpisah**, bukan
  menimpa port lain — sesuai batasanmu.
- **Satu analog stick** — muOS mencantumkan 1 stick untuk RG40XXV.
- Release saat ini: **muOS 2606.0 "Andromeda"** ([release notes](https://muos.dev/release/current/2606_0)).

### Yang BELUM terverifikasi (wajib diukur di perangkat)

| Yang perlu dicek | Cara cek di perangkat | Kenapa penting |
|---|---|---|
| Arsitektur userspace (arm64 vs armhf) | `uname -m` | menentukan target compiler + link flags |
| Versi SDL2 yang terpasang | `ls /usr/lib/libSDL2*` , `strings libSDL2-2.0.so.0 \| grep SDL_VERSION` | contract ke binary CI |
| GLES2 & EGL | `ls /usr/lib/libGLESv2* /usr/lib/libEGL*` | konteks GLES2 |
| **SDL2 video driver** | `SDL_VIDEODRIVER=info` / `strings libSDL2… \| grep -i kmsdrm` | menentukan apakah `SDL_WINDOW_OPENGL` bisa jalan |
| Ekstensi GLES2 | `SDL_GL_GetProcAddress("glGetString")` → `glGetString(GL_EXTENSIONS)` | blending, NPOT texture |
| Input gamepad | `SDL_NumJoysticks()`, `SDL_JoystickName` | mapping D-pad/stick |
| Touchscreen | `SDL_GetNumTouchDevices()` | apakah swipe tersedia |

> **Peringatan khusus:** dokumentasi muOS **tidak** menyebut library GL/EGL
> maupun SDL2 build di image perangkat secara spesifik. Hanya listing perangkat
> + panduan PortMaster. Jadi jangan ikut asumsi "muOS pasti punya libGLESv2
> versi X" — harus dicek langsung, dan jika ternyata SDL2/EGL tidak ada,
> **rencana cadangan** adalah statically-link SDL2 (build sendiri di CI dengan
> arm64 cross-compiler) plus GLES2 lewat EGL, atau memakai driver OpenGL
> software (Mesa llvmpipe/lima) yang jelas lebih lambat tapi selalu ada.

### Rekomendasi build (aman untuk kedua kemungkinan)

1. **Static-link SDL2** ke binary port. Hilangkan ketergantungan pada
   `libSDL2-2.0.so.0` sistem — lalu tidak masalah versinya tidak cocok.
   GLES2/EGL biarkan dinamis (`libGLESv2.so.2`, `libEGL.so.1`) karena itu
   datang dari driver GPU, dan fallback ke Mesa bila perlu.
2. **Target ARM64 (`aarch64-linux-gnu`)** sebagai default; sediakan also ARM
   (`arm-linux-gnueabihf`) kalau `uname -m` ternyata `armv7l`.
3. **GLES2 tanpa shader mnemonic lanjutan**: pakai shader minimum —
   satu textured quad + orthographic matrix, blending `GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA`
   dan additive untuk partikel (`GL_SRC_ALPHA/GL_ONE`). Ini menutupi
   fixed-function GL yang dipakai .exe asli.
4. **Render target internal 800x600**, lalu present dengan letterbox/scale ke
   640x480. Rasio 800:600 = 4:3, dan 640x480 juga 4:3 → **tidak ada
   letterbox, hanya downscale 0.8x**.

Checklist lengkap: `docs/DEVICE_CHECKLIST.md`.

---

## Pertanyaan 4 — Aset binary & hak cipta

Setuju: **aset MumboJumbo tidak boleh masuk repo publik.**

Yang ada di folder ini saat ini: `tools/`, `docs/` — semua kode/dokumentasi
milikmu sendiri, aman untuk repo publik. Yang **tidak** ada dan tidak boleh
gw taruh di sini: salinan `data/` (51,8 MiB sprite+audio+level) dan
`Luxor3.exe`.

Rekomendasi:

- **Repo publik** = kode port (`src/`, `tools/`, `docs/`) saja. Tidak ada aset.
- **Repo privat / LFS** = `assets/` berisi `data/` hasil ekstraksi. Pakai
  Git LFS untuk PNG/OGG besar.
- **CI**: binary hasil build di-*artifact*/release dari repo privat, bukan
  attach binary di repo publik.
- **Kemasan SD card**: folder port terpisah, misalnya
  `/mnt/SDCARD/Ports/Luxor3/` berisi `luxor3` (binary) + `data/` + `.sh`
  launcher — **tidak** menimpa port lain.
- Kalau maurkas binarygame di repo publik: **jangan**. Luxor 3 © 2007
  MumboJumbo (akuisisi MumboJumbo olehFoundation 9). Yang boleh: kode port
  milikmu sendiri, dan data turunan yang kamu buat sendiri (mis. daftar
  level hasil parsing, tabel konstanta) — tapi bukan aset orisinal.

Detail: `docs/ASSET_POLICY.md`.

---

## Temuan tambahan dari implementasi

**Case-sensitivity (blocker nyata di Linux).** Data asli mencampur kapitalisasi:
config menyebut `data/gfx/UI/buttons/MM_top_arrow_Red` sementara file di disk
`MM_top_arrow_red.png`; `data/gfx/Shooter/shooter_` vs folder `Shooter`;
`fx/canopic_jar_spawn.ogg` vs `Canopic_Jar_Spawn.ogg`. Total **202 referensi**
butuh lookup case-insensitive — di NTFS tak masalah, di Linux ARM64 itu
load failure. Solusi: `PathTable` di `src/luxor.h` (index lower-cased, resolve
exact → insensitive → +probe ekstensi). Dicek: **0 collision**, jadi aman.

Detail + tabel lengkap: `docs/COORDINATES.md`, tool `tools/case_audit.py`.

**Sistem koordinat terkonfirmasi.** `screen_y = -world_y` — tanpa offset/skala.
Diverifikasi dengan menggambar track di atas artwork pada 4 map (geb, horus,
ancientroad, CrossingTheNile); hanya transform ini yang membuatnya mengikuti
channel yang digali di batu. Sudut sprite harus ikut dibalik.

**Pipeline sudah jalan (headless, terverifikasi).** Parse `.lvl`/`.LX3TRK`,
resolve aset, decode PNG, rasterisasi sprite, scene, dan preview PNG 800x600 —
semuanya tanpa GPU. 43 assertion headless lulus.

## Status tiap blokir

| Blokir | Status | Keterangan |
|---|---|---|
| #1 Bitness `Luxor3.exe` | ✅ **selesai** | PE32 32-bit x86, x87 dominan, GL 1.x fixed-function, FMOD |
| #2 Isi `data/` | ✅ **selesai** | 3.709 file, 51,8 MiB, format & koordinat dipetakan, 0 referensi rusak, 202 butuh lookup case-insensitive |
| #3 Runtime RG40XXV | ⚠️ **sebagian** | Packaging & resolusi terverifikasi; ABI/SDL2/EGL/GLES2 **wajib diukur di perangkat** |
| #4 Aset di repo publik | ✅ **kebijakan enacted** | `docs/ASSET_POLICY.md`; repo publik = kode saja |

## Rekomendasi urutan lanjut

- [x] **Vertical slice** — loader aset (stb_image), font bitmap, GLES2 sprite
      batcher, preview renderer. Selesai: background + train 10 bola menempel
      rapi di lintasan pada 4 map.
- [ ] **Jalankan `docs/DEVICE_CHECKLIST.md` di perangkat** → `device_report.txt`.
      Ini blocker untuk build ARM64 yang benar-benar kompatibel.
- [ ] **Logika game** — matching bola, collision, skor, combo, powerup.
- [ ] **Audio** — OGG via stb_vorbis/SDL_mixer (FMOD tidak perlu).
- [ ] **UI** — layout dari `UIScreens/*.txt`, font bitmap `.fontdef`.
- [ ] **Kontrol** — D-pad/stick/swipe, remappable.

### Urutan logika yang disarankan

1. Collision grid + matching (warna sama berdekatan >= 3) — ini bagian CPU
   termahal, jadi ukur performanya di perangkat lebih awal.
2. Shooting: balistik, recoil shooter, `LosingTargetVelocity` saat mengenai
   train yang lambat.
3. Skor: `BonusPerComboCollapse` 750, `BonusPerChainedCollapse` 1500,
   `BonusPerAccuracyPercent` 15, `PowerupPointsPerPiece` 100.
4. Progresi level: urutan stage dari `AdventureStages.txt`, difficulty dari
   `flavors_N.txt`, rank dari `Ranks.txt`.
