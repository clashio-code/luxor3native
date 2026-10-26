#!/bin/sh
# Launcher for Luxor3 Native on muOS / PortMaster-style SD card layout.
#
# Usage (from the Ports menu):
#   Ports/Luxor3/luxor3.sh
#
# Environment knobs:
#   LUXOR_RENDERER=2d   force the SDL_Renderer 2D backend (safe default)
#   LUXOR_RENDERER=gl   force GLES2 (fails hard if the driver lacks GL)
#   (unset)             try GL first, fall back to 2D
#   LUXOR_MAX_FRAMES=N  exit after N frames (smoke test)
PORT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$PORT_DIR/data"

# SDL2_mixer (and any other PortMaster-provided libraries) live in
# /opt/zulu/lib on MustardOS, not /usr/lib.
export LD_LIBRARY_PATH="$PORT_DIR:/opt/zulu/lib:${LD_LIBRARY_PATH}"

if [ ! -d "$DATA_DIR" ]; then
    echo "luxor3.sh: missing $DATA_DIR" >&2
    echo "Copy the original game's data/ folder next to this script." >&2
    echo "See docs/ASSET_POLICY.md for what that folder must contain." >&2
    exit 1
fi

# Prefer the GL-capable build; it falls back to 2D on its own when the
# video driver cannot create a GLES2 context.
if [ -x "$PORT_DIR/luxor3_gl" ]; then
    exec "$PORT_DIR/luxor3_gl" "$DATA_DIR" "$@"
elif [ -x "$PORT_DIR/luxor3" ]; then
    exec "$PORT_DIR/luxor3" "$DATA_DIR" "$@"
else
    echo "luxor3.sh: no binary found in $PORT_DIR" >&2
    exit 1
fi
