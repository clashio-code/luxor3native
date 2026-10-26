#!/bin/bash
# muOS / PortMaster launcher for Luxor3 Native.
# PortMaster calls this file from ROMS/Ports; the port payload lives under
# /$directory/ports/luxor3. Keep diagnostics in the payload folder because
# PortMaster menus do not show stdout/stderr.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
    controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
    controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
    controlfolder="$XDG_DATA_HOME/PortMaster"
else
    controlfolder="/roms/ports/PortMaster"
fi

if [ ! -f "$controlfolder/control.txt" ]; then
    echo "PortMaster control file not found: $controlfolder/control.txt" > "$(dirname "$0")/luxor3_error.txt"
    exit 1
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

# This is the same mount convention used by working launchers on the device.
GAMEDIR="/$directory/ports/luxor3"
[ -x "$GAMEDIR/luxor3_gl" ] || [ -x "$GAMEDIR/luxor3" ] || \
    GAMEDIR="$(cd "$(dirname "$0")/../../ports/luxor3" 2>/dev/null && pwd)"

if [ -z "$GAMEDIR" ] || [ ! -d "$GAMEDIR" ]; then
    echo "Luxor3 payload not found. PortMaster directory=/$directory; expected /$directory/ports/luxor3" > "$(dirname "$0")/luxor3_error.txt"
    type pm_finish >/dev/null 2>&1 && pm_finish
    exit 1
fi

cd "$GAMEDIR" || exit 1
LOG="$GAMEDIR/port.log"
rm -f "$LOG"
log() { echo "$*" >> "$LOG"; }

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export LD_LIBRARY_PATH="$GAMEDIR:/opt/zulu/lib:${LD_LIBRARY_PATH}"

log "=== Luxor3 native launcher ==="
log "date=$(date 2>/dev/null)"
log "GAMEDIR=$GAMEDIR"
log "directory=/$directory"
log "CFW_NAME=$CFW_NAME"
log "SDL_GAMECONTROLLERCONFIG=${SDL_GAMECONTROLLERCONFIG}"
log "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
log "uname=$(uname -a 2>/dev/null)"

if [ ! -d "$GAMEDIR/data" ]; then
    log "ERROR: missing $GAMEDIR/data"
    type pm_finish >/dev/null 2>&1 && pm_finish
    exit 1
fi

if [ -x "$GAMEDIR/device_probe" ]; then
    log "--- device_probe ---"
    "$GAMEDIR/device_probe" >> "$LOG" 2>&1
    log "device_probe exit=$?"
fi

if [ -x "$GAMEDIR/luxor3_gl" ]; then
    log "--- launch luxor3_gl ---"
    "$GAMEDIR/luxor3_gl" "$GAMEDIR/data" >> "$LOG" 2>&1
    rc=$?
    log "luxor3_gl exit=$rc"
elif [ -x "$GAMEDIR/luxor3" ]; then
    log "--- launch luxor3 ---"
    "$GAMEDIR/luxor3" "$GAMEDIR/data" >> "$LOG" 2>&1
    rc=$?
    log "luxor3 exit=$rc"
else
    log "ERROR: no executable luxor3 or luxor3_gl found"
    rc=1
fi

log "=== launcher finished (exit=$rc) ==="
type pm_finish >/dev/null 2>&1 && pm_finish
exit "$rc"
