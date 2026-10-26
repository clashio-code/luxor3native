#!/usr/bin/env python3
"""Assemble a clean RG40XXV/muOS folder from the GitHub Actions artifact.

The game data is copyrighted and is deliberately NOT committed to the repo.
This creates the structure shown by the user's muOS SD card:

  <SD root>/ports/luxor3/       binaries + the user's local data/
  <SD root>/ROMS/Ports/luxor3.sh launcher discovered by the Ports menu

Example (Windows PowerShell):
  python tools\\package_sdcard.py `
    --artifact "C:\\Users\\you\\Downloads\\luxor3-aarch64" `
    --sd-root "D:\\" `
    --game-data "C:\\Games\\Luxor 3\\data"

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
    # muOS keeps the launcher in ROMS/Ports and the payload in ports/<name>.
    port_dir = sd_root / "ports" / "luxor3"
    dest_data = port_dir / "data"
    launcher_dir = sd_root / "ROMS" / "Ports"
    launcher_dest = launcher_dir / "luxor3.sh"
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

    launcher_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(launcher_src, launcher_dest)
    launcher_dest.chmod(0o755)

    if dest_data.exists():
        shutil.rmtree(dest_data)
    # copytree preserves case-sensitive names and does not change the user's source.
    shutil.copytree(game_data, dest_data)

    print("Luxor3 port assembled:")
    print(f"  payload:  {port_dir}")
    print(f"  launcher: {launcher_dest}")
    print("Binaries:")
    for name in BINARIES:
        print(f"  {name}")
    print(f"Local game data copied from: {game_data}")
    print("The data folder was copied from your local installation; keep it private.")
    print("The muOS Ports menu should now list Luxor3.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
