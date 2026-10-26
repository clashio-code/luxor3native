#!/usr/bin/env python3
"""Assemble a clean RG40XXV/muOS folder from the GitHub Actions artifact.

The game data is copyrighted and is deliberately NOT copied by this script.
It copies the arm64 executables from the extracted Actions artifact, creates
Ports/Luxor3/ on the destination SD card, and installs the launcher.

Example:
  python3 tools/package_sdcard.py \\
    --artifact /path/to/luxor3-aarch64 \\
    --sd-root /path/to/SDCARD \\
    --game-data "/path/to/your/Luxor 3/data"

The game-data path must point to a data/ folder from a copy you own. It stays
local; the script never uploads or commits it.
"""
import argparse
import os
import shutil
import stat
import sys
from pathlib import Path

BINARIES = ("luxor3", "luxor3_gl", "device_probe")


def fail(message: str) -> "NoReturn":
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--artifact", required=True,
                    help="directory containing extracted luxor3-aarch64 artifact")
    ap.add_argument("--sd-root", required=True,
                    help="root of the mounted SD card (the folder containing Ports/)")
    ap.add_argument("--game-data", required=True,
                    help="local path to the original game's data/ directory")
    ap.add_argument("--replace-data", action="store_true",
                    help="replace existing destination data/ directory (default: stop)")
    args = ap.parse_args()

    artifact = Path(args.artifact).expanduser().resolve()
    sd_root = Path(args.sd_root).expanduser().resolve()
    game_data = Path(args.game_data).expanduser().resolve()
    port_dir = sd_root / "Ports" / "Luxor3"
    dest_data = port_dir / "data"
    launcher_src = Path(__file__).resolve().parents[1] / "sdcard" / "luxor3.sh"
    missing = [name for name in BINARIES if not (artifact / name).is_file()]
    if missing:
        fail(f"artifact is missing binaries: {', '.join(missing)}")
    if not (game_data / "levels").is_dir() or not (game_data / "maps").is_dir():
        fail("--game-data must point to the original data/ folder containing levels/ and maps/")
    if not launcher_src.is_file():
        fail(f"launcher not found: {launcher_src}")
    if not sd_root.is_dir():
        fail(f"SD root does not exist: {sd_root}")
    if dest_data.exists() and not args.replace_data:
        fail(f"destination already has data/: {dest_data}\n"
             "Use --replace-data only if you intentionally want to replace it.")

    port_dir.mkdir(parents=True, exist_ok=True)
    for name in BINARIES:
        source = artifact / name
        target = port_dir / name
        shutil.copy2(source, target)
        target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    shutil.copy2(launcher_src, port_dir / "luxor3.sh")
    (port_dir / "luxor3.sh").chmod(0o755)

    if dest_data.exists():
        shutil.rmtree(dest_data)
    # copytree preserves case-sensitive names and does not change the user's source.
    shutil.copytree(game_data, dest_data)

    print("Luxor3 port folder assembled:")
    print(f"  {port_dir}")
    print("Binaries:")
    for name in BINARIES:
        print(f"  {name}")
    print(f"Local game data copied from: {game_data}")
    print("The data folder was copied from your local installation; keep it private.")
    print("Launch luxor3.sh from muOS Ports. SDL2 2D is the fallback renderer.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
