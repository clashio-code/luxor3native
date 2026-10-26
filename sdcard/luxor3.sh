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

# --- logging ---------------------------------------------------------------
# The Ports menu swallows stdout/stderr, so everything is written to a log
# file that can be read from a PC over the SD card.
LOG=""
for logcandidate in \
    "/mnt/SD2/ports/luxor3/port.log" \
    "/SD2/ports/luxor3/port.log" \
    "/mnt/sdcard/ports/luxor3/port.log" \
    "/sdcard/ports/luxor3/port.log" \
    "/tmp/luxor3.log" ; do
    dir="$(dirname "$logcandidate")"
    if [ -d "$dir" ] && touch "$logcandidate" 2>/dev/null; then
        LOG="$logcandidate"
        break
    fi
done

log() {
    if [ -n "$LOG" ]; then
        echo "$*" >> "$LOG"
    else
        echo "$*"
    fi
}

log "=== luxor3.sh $(date 2>/dev/null) ==="
log "script dir: $(cd "$(dirname "$0")" 2>/dev/null && pwd)"
log "pwd: $(pwd)"
log "uname: $(uname -a 2>/dev/null)"
log "--- mount points ---"
ls -d /mnt/* /sdcard /storage 2>/dev/null | while read -r m; do log "  $m"; done
log "--- candidate port folders ---"

# muOS mounts the SD card at /mnt/SD2, but different images use different
# mount points. The port folder name is checked in both cases because the SD
# card is case-sensitive while the staging drive on a PC is not.
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
    if [ -d "$candidate" ]; then
        log "  found dir: $candidate"
    fi
    if [ -x "$candidate/luxor3_gl" ] || [ -x "$candidate/luxor3" ]; then
        log "  -> using $candidate"
        PORT_DIR="$candidate"
        break
    fi
done

if [ -z "$PORT_DIR" ]; then
    log "ERROR: cannot find the port folder (no luxor3 binary found)"
    log "--- listing of likely roots ---"
    for root in /mnt/SD2/ports /SD2/ports /mnt/sdcard/ports /sdcard/ports /storage/ports; do
        if [ -d "$root" ]; then
            log "  contents of $root:"
            ls -la "$root" 2>&1 | while read -r line; do log "    $line"; done
        fi
    done
    log "=== end of log ==="
    exit 1
fi

DATA_DIR="$PORT_DIR/data"

# SDL2_mixer (and other PortMaster-provided libraries) live in /opt/zulu/lib
# on MustardOS, not /usr/lib.
export LD_LIBRARY_PATH="$PORT_DIR:/opt/zulu/lib:${LD_LIBRARY_PATH}"
log "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
log "PORT_DIR=$PORT_DIR"
log "DATA_DIR=$DATA_DIR"

if [ ! -d "$DATA_DIR" ]; then
    log "ERROR: missing $DATA_DIR"
    log "=== end of log ==="
    exit 1
fi

log "--- port folder contents ---"
ls -la "$PORT_DIR" 2>&1 | while read -r line; do log "  $line"; done

cd "$PORT_DIR" || exit 1

# Report the runtime facts that decide which renderer can work.
log "--- linked libraries ---"
if command -v ldd >/dev/null 2>&1; then
    ldd "$PORT_DIR/luxor3_gl" 2>&1 | while read -r line; do log "  $line"; done
fi

log "--- video driver probe ---"
if [ -x "$PORT_DIR/device_probe" ]; then
    "$PORT_DIR/device_probe" 2>&1 | while read -r line; do log "  $line"; done
fi

# Prefer the GL-capable build; it falls back to 2D on its own when the video
# driver cannot create a GLES2 context.
if [ -x "$PORT_DIR/luxor3_gl" ]; then
    log "--- launching luxor3_gl ---"
    "$PORT_DIR/luxor3_gl" "$DATA_DIR" "$@" 2>&1 | while read -r line; do log "  $line"; done
    rc=$?
    log "luxor3_gl exited with $rc"
elif [ -x "$PORT_DIR/luxor3" ]; then
    log "--- launching luxor3 (2D only) ---"
    "$PORT_DIR/luxor3" "$DATA_DIR" "$@" 2>&1 | while read -r line; do log "  $line"; done
    rc=$?
    log "luxor3 exited with $rc"
else
    log "ERROR: no binary found in $PORT_DIR"
    rc=1
fi

log "=== end of log ==="
exit $rc
