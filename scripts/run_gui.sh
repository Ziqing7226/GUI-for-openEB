#!/usr/bin/env bash
# scripts/run_gui.sh — launcher for GUI-for-openEB.
#
# Copy this script to scripts/run_gui.local.sh and customise the env vars
# for your camera hardware.  The .local.sh copy is git-ignored so you can
# keep your private HAL plugin path there.
#
# Common HAL plugin paths:
#   Prophesee (default openEB install): /usr/local/lib/metavision/hal/plugins
#   CenturyArks:                        /usr/lib/CenturyArks/hal/plugins
set -euo pipefail

# ---- resolve repo root + binary ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$REPO_ROOT/build/gui/gui_for_openeb"

if [[ ! -x "$BIN" ]]; then
    echo "[run_gui.sh] Binary not found at: $BIN" >&2
    echo "[run_gui.sh] Run 'cmake -B build && cmake --build build' first." >&2
    exit 1
fi

# ---- OpenEB runtime environment (customise for your camera) ----

# SDK shared libraries (needed if installed outside the standard linker path).
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:/usr/local/lib"

# HDF5 plugin path (for reading .hdf5 event files).
export HDF5_PLUGIN_PATH="${HDF5_PLUGIN_PATH:-}:/usr/local/lib/hdf5/plugin"

# HAL plugin path — camera driver .so files.
# Change this to match your camera vendor's install location.
#   - Prophesee:  /usr/local/lib/metavision/hal/plugins
#   - CenturyArks: /usr/lib/CenturyArks/hal/plugins
export MV_HAL_PLUGIN_PATH="${MV_HAL_PLUGIN_PATH:-/usr/local/lib/metavision/hal/plugins}"

# ---- Qt platform plugin ----
# On Wayland sessions Qt 6 may fail to render via XWayland (black window).
# Force native Wayland if the session is Wayland; otherwise let Qt auto-detect.
if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
fi

# Suppress harmless GTK/dconf warnings from Qt's GTK theme integration.
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-*.warning=false}"

# ---- launch ----
exec "$BIN" "$@"
