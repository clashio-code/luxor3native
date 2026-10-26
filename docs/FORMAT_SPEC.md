# Luxor 3 — Format Data (hasil investigasi)

Semua file di `data/` adalah **teks biasa**: komentar `//` sampai akhir baris, blok `{ }`,
entri berupa `key value`, `key { ... }`, atau daftar nilai telanjang.

> Verifikasi: `tools/luxor_parse.py` mem-parse seluruh 223 file `.lvl` tanpa error,
> dan `tools/asset_audit.py` menemukan **0 referensi yang tidak resolve** dari total
> 2.479 referensi (anim, texture, effect, sound, locale).

## 1. Tata letak folder

```
data/
  AnimDefs.txt          defisi animasi (117 Sequence)
  CustomCursors.txt
  DifficultyGameVariables.txt
  GamePieceFlavors.txt  set bola + tint RGB
  GameSounds.txt        Music/Sound -> file ogg
  GameVariables.txt     konstanta gameplay (nilai numerik)
  PowerupRules.txt      definisi powerup + level/duration
  Ranks.txt             ambang skor -> rank
  ShooterDefs.txt       3 shooter + offset peluru
  TreasureFlavors.txt
  UIScreenLayout.txt    include semua UIScreens/*.txt
  UIStyles.txt
  UIScreens/            28 file layout UI
  effects/              264 file .effect (sistem partikel)
  gfx/                  2.697 PNG + 43 JPG (sprite; sebagian besar gambar ada di gfx/)
  levels/               223 .lvl + 93 .txt include
  locale/               fonts/Default_Common.fontdef + strings/English.txt
  maps/                 28 folder map
  sound/                fx/ (95) music/ (5) ui/ (8)  -> 108 .ogg
  UI screens/           42 `.txt` screen definitions under `data/UIScreens/`
```

> Catatan hitung PNG/JPG: 2.739 PNG total (2.697 di `gfx/`, 39 di `maps/`,
> 1 font atlas, 2 installer splash); 100 JPG total (43 di `gfx/`, 55 di `maps/`,
> 2 installer logos). Game `data/` sendiri berisi 2.737 PNG dan 98 JPG.

## 2. `.lvl` — definisi level

```
MapPath "data/maps/geb/"          # map yang dipakai
LevelDisplayName "#geb"           # key lokalisasi
LevelMusic "LevelMusicClassic"    # key dari GameSounds.txt
TrainConstructionData { NumSpheresToSpawnWith 25  ColorSpawnStreak 0.5 }
NumTrainsToSpawn 4
MinSecondsBeforeNextTrainCanSpawn 25.0
StartingTrackSectionSpawnOrder { initial }   # urutan section tempat train mulai
#include "flavors_1.txt"                      # relatif ke folder .lvl
#include "treasure_1.txt"
```

Enam mode, ditentukan oleh isi (bukan nama folder):

| Mode | Ciri pembeda | Jumlah |
|---|---|---|
| `classic` | punya `StartingTrackSectionSpawnOrder` | 76 |
| `crossingTheNile` | punya blok `TrainTrack { TrackSection ... }` | 29 |
| `centipede` | punya `WinTrain { TrackSection ... }` + `Train { FlavorList }` | 9 |
| `puzzle` | punya `Train { TrackSegment ... FlavorList }` | 62 |
| `survival` | classic + `survivalData.txt` (upgrade train per difficulty) | 35 |
| `treasureHunter` | `LevelLengthSeconds` + `Min/MaxSpeedScale` + `BuildUpSpawnInterval` | 12 |

Tambahan per mode:

- **crossingTheNile** — tiap `TrainTrack` punya `TrackSection`, `Speed`, `NumSpheresToSpawnWith`,
  `ColorSpawnStreak`, `LengthWhenNextTrainSpawns`. Ada juga `BonusTime 300`.
- **centipede** — `WinTrain { TrackSection "targetline"  Speed 20  Flavors { ... } }`
  plus `MinMushroomCount/IdealMushroomCount/MaxMushroomCount`, `MushroomRespawnInterval`.
- **puzzle** — `Ammo { Green Red }` (bola yang boleh ditembak) + `Train { TrackSegment "flowerR"
  TrackSegmentOffset 80.0  FlavorList { ... } }`.

### `#include`

- Resolve **relatif ke folder file yang meng-include**.
- 374 include dalam 223 `.lvl`; semua resolve (0 rusak).
- Tipe include: `flavors_N.txt` (AllowableGamePieceFlavors per difficulty),
  `treasure_N.txt` (AllowableTreasureFlavors), `survivalData.txt`, `powerups.txt`.
- `flavors_N.txt` memuat blok `AllowableGamePieceFlavors { Easy {...} Medium {...} ... }`;
  tiap difficulty punya daftar warna berbeda (Easy 4 warna → Insane 9 warna).
- `treasure_N.txt` memuat `AllowableTreasureFlavors { Gem01 Gem02 Gem03 Ring01 }`.

## 3. `path.LX3TRK` — geometri lintasan

Format tekstual, 29 file (28 map + `maps/puzzle/queuepath.LX3TRK`).

```
Verts
{
    Vert -18.5 -168.5 0        # x, y, (z unused)
    ...
}

Sections
{
    "main03" { 0 1 2 3 ... 56 }   # daftar index vertex
    "occlude01" { 174 0 }
}

Reflectors { }
NamedPoints
{
    "point_spawn01" ( 372.158, -43.6727 )
    "pointgate01_pivot" ( 570, -110 )
}
```

- Koordinat: pixel dalam ruang yang sama dengan artwork **800x600**, dengan sumbu
  Y terbalik (negatif = ke bawah layar); koordinat vertex bisa keluar sedikit
  dari canvas karena ujung jalur/entry (rentang x ∈ [-36, 840], y ∈ [-486, +40]).
- **Viewport**: background/mask 800x600. Karena data level lain memakai koordinat
  ini sebagai piksel, render game pada logical 800x600; tidak perlu offset global.
- Section = polyline dari vertex yang diindeks. Panjang tiap section vary
  (mis. `geb/main01a` 726 px, `geb/gate1-1` 46 px).
- Section bisa **berbaca/bercabang**: `gate1-2` memakai ulang vertex `160` (titik
  junction), `merge1-1` memakai `114`, `115`.
- `pathGamePlayData.txt` (per map) menambah: `StartSpeed/EndSpeed <section>`,
  `SwitchState <gate> <leg>`, `SwitchGate { Pivot/Leg }`, `Occluded <section>`,
  `Unshootable <section>`, `SpawnRestrictive <section>`, `Danger <section>`,
  `MinScale/MaxScale`.

## 4. `AnimDefs.txt` — animasi

```
Sequence
{
    name        "PusherWalk"
    fileName    "data/gfx/pusher/grunt/walk_"     # prefix, frame = <prefix>NNNN.png
    fileNamePlayBackwards "data/gfx/other"         # opsional
    playbackRate 24
    isLooping  1
    weight      5.0
}
```

- 117 `Sequence`, **97 referensi file unik**; 3 menunjuk file langsung,
  94 menunjuk *frame family* (`walk_0001.png` … `walk_0024.png`).
- Nama animasi berduplikasi = pilih acak berbobot (lihat `PusherWalk`).
- 2 referensi pakai separator Windows (`data\gfx\...`) → normalisasi ke `/`.

## 5. `.effect` — sistem partikel

```
EffectDefinition
{
    EffectComponent ParticleEffect "ball_puff"
    {
        RepeatTime      0
        ParticleLifespan [0.3,0.4]
        BlendMode       add
        Texture         "data/gfx/particles/smoke_01.png"

        EmitterEvents { SpawnParticles { count=5 speed=[100,140] ... }
                        Dynamics { delay=0, absoluteAcceleration=(0,-100) } }
        ParticleEvents { RGB { age=0, rgb=(30,30,255) }
                         Alpha { age=0, alpha=255  age=1, alpha=0 }
                         Scale { age=0, scale=(0.5,0.5)  age=1, scale=(1.5,1.5) } }
    }
}
```

264 file, 960 referensi Texture (semua resolve). Keyframe age ternormalisasi 0..1.
`BlendMode`(add/…) menentukan blend GL.

## 6. `Placeables.txt` (per map)

```
CanopicJarSpawnPoints { pos "point_spawn01" ... }
Placeable {
    HighDetailOnly
    drawLayer "AboveEverything"
    name "sun_glow"
    effect "Ancient_Road_Sun_01"     # -> data/effects/<nama>.effect
    pos (700.0, -100.0)
    scale 1.0
    trackSectionLink "main01"
}
Reflections 0
```
157 referensi effect, semua resolve.

## 7. Aset gambar

| Format | Jumlah | Ukuran | Catatan |
|---|---|---|---|
| PNG | 2.739 | 24,8 MiB | 2.673 RGBA8, 50 Gray8, 14 RGB8, 2 Palette2; **tidak ada interlace** |
| JPG | 100 | 8,8 MiB | 35× 800x600 (background), 27× 151x108 (thumb), 22× 80x600 |

- Ukuran sprite paling umum **32x32** (1.739 file) — ini ball/frame animation.
- Semua mask `*_bgmask.png` / `*_fgmask.png` = **800x600** (alpha mask layer).
- Format PNG non-interlaced 8-bit → aman untuk loader GLES2 sederhana
  (pakai `stb_image` / `SDL2_image`).

## 8. Audio

- 108 file **.ogg** (bukan WAV) — `music/` 5, `fx/` 95, `ui/` 8.
- `GameSounds.txt` memetakan `Music`/`Sound` → `file "music/theme_classic.ogg"`.
- FMOD (v3, `fmod.dll` 159 KiB) dipakai di `.exe` asli → port cukup baca OGG
  langsung via SDL_mixer / stb_vorbis, **tanpa FMOD**.

## 9. Font & lokalisasi

- `locale/fonts/Default_Common.fontdef`: bitmap font, tiap char punya
  `FontPage`, `Position`, `Size`, `ACWidths`. Halaman PNG: `Default_Common0.png`.
- `locale/strings/English.txt`: 662 key `#Name "Text"`.
  643 referensi `#Key` dari file lain — semua resolve.

## 10. Konstanta gameplay (`GameVariables.txt`)

Semua nilai ada di sini — ini acuan implementasi fisika:

| Konstanta | Nilai | Arti |
|---|---|---|
| `DefaultTrackStartSpeed` | 800.0 | px/detik saat train mulai (bukan di path) |
| `DefaultTrackEndSpeed` | 2.0 | px/detik di ujung track |
| `TrainAcceleration` | 200.0 | akselerasi train |
| `TrainHighAcceleration` | 800.0 | akselerasi saat banyak train |
| `MaxTurnSpeedInDegreesPerSecond` | 120.0 | batas belok train |
| `LosingTargetVelocity` | 1000.0 | kecepatan bola yang terdegradasi |
| `ShooterShotRecoilRatePerSecond` | 200.0 | recoil shooter |
| `ShooterLengthOfRecoil` | 10.0 | px |
| `ShooterFullTiltAmount` | 50.0 | px untuk tilt penuh |
| `ShooterTiltRateOfDecay` | 2.5 | laju kembali netral |
| `ShooterSecondsBeforeNextShot` | 0.5 | jeda antar tembakan |
| `BonusPerComboCollapse` | 750 | poin |
| `BonusPerChainedCollapse` | 1500 | poin |
| `BonusPerAccuracyPercent` | 15 | poin per % akurasi |
| `PowerupPointsPerPiece` | 100 | poin per powerup |
| `AnkhCoinPointValue` | 250 | koin |
| `CanopicJarPointValue` | 1000 | jar |

Koordinat shooter: `primaryAmmoOffset (0,-14)`, `secondaryAmmoOffsets (-16,10) (16,10)`.

## 11. Ringkasan temuan untuk port

1. **Tidak ada data biner** — semua format teks, parsing jauh lebih mudah dari dugaan awal.
2. **Tidak ada model 3D** — semua sprite 2D PNG.
3. **Tren CPU ringan** — logika game (matching bola, collision) adalah bagian mahal,
   bukan rendering. GLES2 cukup untuk sprite 2D.
4. **Audio OGG** — tidak perlu FMOD.
5. **Font bitmap** — tidak perlu TTF.
6. Aset asli **berlisensi MumboJumbo** → jangan masuk repo publik (lihat `ASSET_POLICY.md`).
