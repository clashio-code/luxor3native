# Sistem Koordinat & Gotcha Linux (hasil verifikasi)

## 1. Dua ruang koordinat

| Ruang | Sumbu Y | Sumber |
|---|---|---|
| **World** (data `.LX3TRK`) | ke **ATAS** | game asli memakai `glOrtho(0, 800, 0, 600)` |
| **Screen** (render) | ke **BAWAH** | konvensi GPU/SDL |

Konversi: `screen_y = -world_y`. Tidak ada offset, tidak ada skala.

**Bukti empirik** (bukan asumsi): polyline track digambar magenta di atas artwork
pada 4 map; hanya transform ini yang membuat track mengikuti channel yang digali di batu.

- `geb` — track = alur batu di kiri-atas; cocok persis.
- `horus` — track = relief bx yang digali; cocok persis.
- `ancientroad` — track = jalur tanah; cocok.
- `CrossingTheNile` — track = kolom batu melintang; cocok.

Kode: `src/luxor.h` (`kWorldToScreenY = -1.0f`), konversi tunggal di
`src/scene.cpp::build_scene`.

### Konsekuensi untuk sudut

Mencerminkan Y juga mencerminkan sudut tangent. Jadi sudut sprite harus
**dibalik** (`angle = -tangent`), kalau tidak bola akan terbalik saat train
berbelok ke bawah. Sudah diterapkan di `build_scene`.

### Batas kanvas

- Background & mask: **800 x 600**.
- Track geb: world x [36.5, 780], y [-485, 20] → screen y [-20, 485].
- Overhang kecil di atas frame (`screen_y` negatif) itu ** disengaja**: train
  masuk dari luar layar. Ball yang punya `s < 0` diklempar ke posisi awal.
- Titik spawn canopic jar **semua** di dalam frame (diverifikasi lewat test).

## 2. ⚠️ PATH TIDAK CASE-CONSISTENT — blocker utama di Linux

Data asli Data asli memakai kapitalisasi yang tidak konsisten:

```
referensi di file config : data/gfx/UI/buttons/MM_top_arrow_Red
file di disk              : data/gfx/UI/buttons/MM_top_arrow_red.png
referensi di file config : data/gfx/placeables/geb/feather_01.png
file di disk              : data/gfx/placeables/Geb/feather_01.png
referensi di file config : fx/canopic_jar_spawn.ogg
file di disk              : data/sound/fx/Canopic_Jar_Spawn.ogg
```

Di Windows ini tidak masalah (NTFS case-insensitive). Di Linux ARM64 —
filesystem case-sensitive — itu **load failure**.

### Hitungan

| Kategori | Ref | Butuh case-insensitive |
|---|---|---|
| UI / texture | 521 | **179** |
| `.effect` texture | 960 | **7** |
| `GameSounds` | 101 | **3** |
| Placeable effect | 157 | **13** |
| Animasi (`fileName`) | 105 | 0 (semua via prefix) |
| **Total** | | **202** |

Sebanyak 202 referensi akan gagal di filesystem case-sensitive.

### Solusi (sudah diterapkan)

`src/luxor.h` → `class PathTable`:

1. Scan seluruh tree sekali, index **lower-cased** → path asli.
2. `resolve(ref)`: coba exact → case-insensitive →+tprobe ekstensi (`.png`,
   `.jpg`, `.ogg`, `.effect`, `.txt`, `.fontdef`, `.LX3TRK`).
3. `resolve_under(base, ref)`: untuk referensi yang relatif ke sub-root
   (audio relatif ke `data/sound`, bukan ke root data).
4. Separator `\` dinormalisasi ke `/` (ada referensi Windows-style di config).

Semua lookup texture/audio di port **harus lewat `PathTable`**, tidak boleh
`fopen()` langsung. Ini jugaDdorroh hemat: table di-build sekali (~3.709 file,
< 10 ms), bukan per lookup.

### Tidak ada collision

Dicek: **0** file berbeda hanya karena kapitalisasi. Jadi table case-insensitive
aman — tidak ada dua file yang mauDiTabrakkan.

## 3. Format path relatif per kategori

| Jenis | Path relatif ke | Contoh |
|---|---|---|
| `.effect` Texture | root game | `data/gfx/particles/smoke_01.png` |
| UI texture | root game | `data/gfx/UI/MainMenu/logo_` |
| `GameSounds` file | `data/sound/` | `fx/canopic_jar_spawn.ogg` |
| `Placeables` effect | root game (nama + `.effect`) | `Ancient_Road_Sun_01` |
| `AnimDefs` fileName | root game | `data/gfx/pusher/grunt/walk_` |
| `.lvl` `#include` | folder `.lvl` itu sendiri | `flavors_1.txt` |
| `MapPath` | root game | `data/maps/geb/` |

Perhatikan kategori **kedua**: `GameSounds.txt` menulis `fx/…` relatif ke
`data/sound/`, bukan ke root. Ini|source of bug kalau path resolution naïf.

## 4. Spasi & line ending

- Semua file teks memakai LF, encoding ASCII/CP1252 (bukan UTF-8) → parser
  C++ membaca byte mentah, bukan localized char.
- Some entries punya trailing whitespace / tab → tokenizer harus skip.
- Komentar `//` hanya baris, tidak block comment.

## 5. Gotcha lain

- **32x32 = 1.739 file** — itu bola + frame animasi. Size ball texture = 32,
  spacing bola di train = 32 (diameter), bukan radius.
- Gambar PNG: 2.673 RGBA8, 50 Gray8, 14 RGB8, 2 Palette2, **0 interlace**.
  `stb_image` aman untuk semuanya (8-bit gray/palette ditangani).
- `stb_image` **tidak** bisa decode JPG alpha, tapi background map memang JPG
  tanpa alpha (solid).
- Font bukan TTF → bitmap atlas (`Default_Common.fontdef` +
  `Default_Common0.png`). Tidak perlu TTF/SDL_ttf di port.
