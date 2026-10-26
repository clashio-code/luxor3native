#!/usr/bin/env python3
"""Generate a synthetic mini-game so CI can test without copyrighted assets.

The real Luxor 3 data tree is copyrighted (see docs/ASSET_POLICY.md) and must
never enter the repository or CI. This script builds a tiny stand-in with the
same shapes: a track file, a level file with an #include, a ball sprite and a
map background.

What it deliberately exercises:
  * .LX3TRK parsing: multiple sections, a junction that shares vertices
  * .lvl parsing: MapPath, TrainConstructionData, spawn order, #include
  * world-to-screen Y conversion
  * case-insensitive path resolution
  * PNG decoding (RGBA, alpha)

Usage: python3 make_fixture.py <output-dir>
"""
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- png writer
def png_rgba(width, height, pixel_fn):
    """Minimal RGBA8 PNG encoder (no interlacing)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)                       # filter type: none
        for x in range(width):
            raw.extend(pixel_fn(x, y))

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) +
            chunk(b'IEND', b''))


def ball_pixel(x, y):
    cx = cy = 15.5
    d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
    if d <= 12.0:
        return (220, 30, 30, 255)
    if d <= 14.0:
        return (255, 255, 255, 255)
    return (0, 0, 0, 0)


def solid_pixel(r, g, b, a):
    return lambda x, y: (r, g, b, a)


# ---------------------------------------------------------------- content
TRACK = """// Synthetic track fixture for CI.
// Mirrors the structure of a real path.LX3TRK: world Y points up.

Verts
{
\tVert 100 0 0
\tVert 100 -50 0
\tVert 50 -50 0
\tVert 50 -100 0
\tVert 150 -100 0
\tVert 150 -150 0
}

Sections
{
\t"main" { 0 1 }
\t"branch" { 2 3 }
\t"junction" { 1 2 }
\t"exit" { 4 5 }
}

Reflectors
{
}

NamedPoints
{
\t"point_spawn01" ( 120, -25 )
\t"point_spawn02" ( 60, -75 )
}
"""

FLAVORS = """AllowableGamePieceFlavors
{
\tEasy
\t{
\t\tYellow
\t\tRed
\t}
}
"""

LEVEL = """MapPath "data/maps/testmap/"
LevelDisplayName "#test"
LevelMusic "LevelMusicClassic"

TrainConstructionData
{
\tNumSpheresToSpawnWith\t7
\tColorSpawnStreak\t0.25
}

NumTrainsToSpawn\t2
MinSecondsBeforeNextTrainCanSpawn 25.0

StartingTrackSectionSpawnOrder
{
\tmain
}

#include "flavors_1.txt"
"""

MAP_INFO = """// Synthetic map fixture.
MapName "#testmap"
"""

PATH_GAMEPLAY = """// Synthetic path gameplay metadata for parser CI.
MinScale 0.85
MaxScale 1.0
StartSpeed main 120
EndSpeed main 70
Danger branch
Occluded hidden
Unshootable hidden
SpawnRestrictive branch
Underwater exit
SwitchState main branch
SwitchGate
{
    Pivot "main" "point_pivot"
    Leg "branch" "point_leg"
}
"""

GAME_VARIABLES = """// Synthetic variables fixture for parser CI.
TrainAcceleration "200.0"
TrainHighAcceleration "800.0"
DefaultTrackStartSpeed "800.0"
DefaultTrackEndSpeed "2.0"
ShooterSecondsBeforeNextShot "0.5"
BonusPerComboCollapse "750"
#include "difficulty_extra.txt"
"""

DIFFICULTY_EXTRA = """DifficultyTest "1.25"
"""

DIFFICULTY_VARIABLES = """// Synthetic difficulty variables fixture for parser CI.
EasySpawnStreak "0.5"
HardSpawnStreak "0.75"
"""


def write(path, data, binary=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = 'wb' if binary else 'w'
    with open(path, mode, **({} if binary else {'encoding': 'latin1'})) as f:
        f.write(data)


def main(out_dir):
    root = os.path.abspath(out_dir)
    write(os.path.join(root, 'data/maps/testmap/path.LX3TRK'), TRACK)
    write(os.path.join(root, 'data/maps/testmap/MapInfo.txt'), MAP_INFO)
    write(os.path.join(root, 'data/maps/testmap/pathGamePlayData.txt'), PATH_GAMEPLAY)
    write(os.path.join(root, 'data/levels/classic/flavors_1.txt'), FLAVORS)
    write(os.path.join(root, 'data/levels/classic/T_1_1.lvl'), LEVEL)
    write(os.path.join(root, 'data/GameVariables.txt'), GAME_VARIABLES)
    write(os.path.join(root, 'data/DifficultyGameVariables.txt'), DIFFICULTY_VARIABLES)
    write(os.path.join(root, 'data/difficulty_extra.txt'), DIFFICULTY_EXTRA)
    write(os.path.join(root, 'data/gfx/balls/red/ball0000.png'),
          png_rgba(32, 32, ball_pixel), binary=True)
    write(os.path.join(root, 'data/maps/testmap/back_01.png'),
          png_rgba(8, 8, solid_pixel(20, 40, 120, 255)), binary=True)

    total = sum(len(files) for _, _, files in os.walk(root))
    print(f"fixture written to {root} ({total} files)")
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
