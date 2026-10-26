#!/bin/sh
# Launcher for Luxor3 Native on muOS / PortMaster-style SD card layout.
#
# Layout on the SD card:
#   /ROMS/Ports/luxor3.sh   <- this file (what the muOS Ports menu runs)
#   /ports/luxor3/          <- binaries and the local data/ folder
#
# Environment knobs:
#   LUXOR_RENDERER=2d   force the SDL_Renderer 2D backend (safe default)
#   LUXOR_RENDERER=gl   force GLES2 (fails hard if the driver lacks GL)
#   (unset)             try GL first, fall back to 2D
#   LUXOR_MAX_FRAMES=N  exit after N frames (smoke test)

# muOS mounts the SD card at /mnt/SD2, but different images use different
# mount points. Try the known ones instead of hard-coding a single path.
# The port folder name is checked in both cases because the SD card is
# case-sensitive while the staging drive on a PC is not.
PORT_DIR=""
for candidate in \
    "/mnt/SD2/ports/luxor3" \
    "/mnt/SD2/ports/Luxor3" \
    "/SD2/ports/luxor3" \
    "/SD2/ports/Luxor3" \
    "/mnt/sdcard/ports/luxor3" \
    "/mnt/sdcard/ports/Luxor3" \
    "/sdcard/ports/luxor3" \
    "/sdcard/ports/Luxor3" \
    "/storage/ports/luxor3" \
    "/storage/ports/Luxor3" ; do
    if [ -x "$candidate/luxor3_gl" ] || [ -x "$candidate/luxor3" ]; then
        PORT_DIR="$candidate"
        break
    fi
done

if [ -z "$PORT_DIR" ]; then
    echo "luxor3.sh: cannot find the port folder (looked for luxor3 binary)." >&2
    echo "Expected it at /mnt/SD2/ports/luxor3 on the SD card." >&2
    exit 1
fi

DATA_DIR="$PORT_DIR/data"

# SDL2_mixer (and other PortMaster-provided libraries) live in /opt/zulu/lib
# on MustardOS, not /usr/lib.
export LD_LIBRARY_PATH="$PORT_DIR:/opt/zulu/lib:${LD_LIBRARY_PATH}"

if [ ! -d "$DATA_DIR" ]; then
    echo "luxor3.sh: missing $DATA_DIR" >&2
    echo "Copy the original game's data/ folder into the port folder." >&2
    exit 1
fi

cd "$PORT_DIR" || exit 1

# Prefer the GL-capable build; it falls back to 2D on its own when the video
# driver cannot create a GLES2 context.
if [ -x "$PORT_DIR/luxor3_gl" ]; then
    exec "$PORT_DIR/luxor3_gl" "$DATA_DIR" "$@"
elif [ -x "$PORT_DIR/luxor3" ]; then
    exec "$PORT_DIR/luxor3" "$DATA_DIR" "$@"
else
    echo "luxor3.sh: no binary found in $PORT_DIR" >&2
    exit 1
fi
