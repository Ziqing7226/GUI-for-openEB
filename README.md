# GUI for openEB

A Qt 6 graphical user interface for [openEB](https://github.com/prophesee-ai/openeb) event cameras, plus a self-developed algorithm library (`algo/`).

**Current stage: Phase 2 complete** — see [Development Roadmap](#development-roadmap).

---

## Features

### Phase 1 — MVP (delivered)
- CMake project skeleton (`openeb/` + `gui/` + `algo/` three-layer architecture)
- Camera discovery & connection (live camera + offline file playback)
- Real-time event display via OpenGL (GLSL 330 core, letterboxed viewport)
- Basic display parameters (accumulation time 1–1000 ms, 4 colour themes)
- Statistics panel (event rate, peak rate, ON/OFF ratio, FPS, timestamp)
- `algo_bridge` interface skeleton (full 46-algorithm registry)
- `algo/common/` utilities: lock-free SPSC ring buffer, multi-window frame generator, offline data loader

### Phase 2 — Camera control panels (delivered)
- **Biases panel**: dynamic enumeration of all HAL biases, slider + spinbox + reset, save/load `.bias` files
- **ROI panel**: rectangle ROI/RONI via `I_ROI`, drag-to-select on display widget, apply/clear
- **ESP panel**: Anti-Flicker (mode/band/preset/duty/threshold), Trail Filter (type/threshold), ERC (target event rate)
- **Trigger panel**: Trigger In (per-channel enable) + Trigger Out (enable/period/duty)

All panels gracefully degrade when the connected device lacks the corresponding HAL facility (e.g. file playback disables all four panels).

---

## Directory structure

```
GUI-for-openEB/
├── openeb/                   # openEB SDK subtree (Apache 2.0, v5.2.0)
├── gui/                      # GUI application (C++ / Qt 6)
│   ├── main.cpp              # Application entry, env-var defaults
│   ├── main_window.{h,cpp}   # Main window: menus, docks, signal wiring
│   ├── display/              # OpenGL event display widget
│   ├── panels/               # Settings dock panels (Devices/Info/Stats/
│   │                         #   Display/Biases/ROI/ESP/Trigger/…)
│   ├── app/                  # Controllers (camera/frame_pipeline/statistics)
│   ├── algo_bridge/          # Algorithm registry & bridge to algo/
│   └── CMakeLists.txt
├── algo/                     # Self-developed algorithm library (C++)
│   ├── common/               # Event ring buffer, frame generator, data loader
│   └── CMakeLists.txt
├── scripts/
│   └── run_gui.sh            # Launcher script (env-var setup)
├── doc/
│   ├── design.md             # Full design specification (10-phase roadmap)
│   └── compile.md            # Build guide (Ubuntu 26.04 / GCC 15)
├── LICENSE                   # MIT (project's original code)
└── README.md
```

---

## Requirements

| Component | Version |
|-----------|---------|
| OS | Ubuntu 26.04 (or compatible Linux) |
| Compiler | GCC 15+ |
| CMake | 4.x |
| Qt | 6.x (Widgets, OpenGL, OpenGLWidgets) |
| OpenEB SDK | 5.2.0 |
| OpenCV | 4.x |
| Python | 3.12 (only if building openEB from source) |

See [doc/compile.md](doc/compile.md) for detailed OS setup, including the GCC 15 `<cstdint>` fix and Python 3.12 via deadsnakes PPA.

---

## Building

```bash
# 1. Ensure openEB SDK is installed (see doc/compile.md)
# 2. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build --config Release -- -j$(nproc)
```

The binary is output to `build/gui/gui_for_openeb`.

---

## Running

### Quick start (launcher script)

```bash
./scripts/run_gui.sh
```

The script auto-detects Wayland sessions and sets `QT_QPA_PLATFORM=wayland`. Edit it or copy to `scripts/run_gui.local.sh` to customise the HAL plugin path for your camera.

### Manual launch

```bash
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:/usr/local/lib"
export HDF5_PLUGIN_PATH="${HDF5_PLUGIN_PATH:-}:/usr/local/lib/hdf5/plugin"
export MV_HAL_PLUGIN_PATH=/usr/local/lib/metavision/hal/plugins  # Prophesee
# export MV_HAL_PLUGIN_PATH=/usr/lib/CenturyArks/hal/plugins     # CenturyArks

# Wayland sessions should force the Wayland platform plugin:
export QT_QPA_PLATFORM=wayland

./build/gui/gui_for_openeb
```

### Environment variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `MV_HAL_PLUGIN_PATH` | Camera HAL plugin directory | `/usr/local/lib/metavision/hal/plugins` |
| `HDF5_PLUGIN_PATH` | HDF5 plugin directory (for `.hdf5` files) | `/usr/local/lib/hdf5/plugin` |
| `LD_LIBRARY_PATH` | SDK shared library search path | (must include `/usr/local/lib`) |
| `QT_QPA_PLATFORM` | Qt platform plugin | `wayland` if `WAYLAND_DISPLAY` is set |

> **Wayland note**: On Wayland sessions, Qt may render a black window via XWayland. Force `QT_QPA_PLATFORM=wayland` to use the native Wayland plugin. If native Wayland is unavailable, fall back to `QT_QPA_PLATFORM=xcb`.

### Camera vendors

The `MV_HAL_PLUGIN_PATH` must point to your camera vendor's HAL plugin directory:

| Vendor | Plugin path |
|--------|-------------|
| Prophesee (default openEB) | `/usr/local/lib/metavision/hal/plugins` |
| CenturyArks | `/usr/lib/CenturyArks/hal/plugins` |

If the env var is already exported in your shell, the application respects it; otherwise it falls back to the Prophesee default.

---

## Development roadmap

Based on [doc/design.md](doc/design.md) (10-phase plan):

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | CMake skeleton, camera discovery, OpenGL display, basic params, stats panel, algo_bridge skeleton | **Done** |
| 2 | Bias / ROI / ESP / Trigger control panels | **Done** |
| 3 | Recording, playback, file cutter | Pending |
| 4 | Export / convert (RAW↔HDF5↔CSV↔DAT) | Pending |
| 5 | Event filter chain + 7 preprocessors | Pending |
| 6 | Self-developed CV algorithms (noise filter, optical flow, blob, tracker, …) | Pending |
| 7 | Analytics algorithms (active marker, event-to-video) | Pending |
| 8 | Calibration (intrinsic / extrinsic) | Pending |
| 9 | Multi-window layout (Temporal plot, algo result windows) | Pending |
| 10 | Internationalisation (i18n), polish, packaging | Pending |

---

## License

### Original code of this project

Licensed under the [MIT License](LICENSE).

### Referenced openEB code

This project references [openEB](https://github.com/prophesee-ai/openeb) (version 5.2.0).

openEB is licensed under the [Apache License 2.0](openeb/licensing/LICENSE_OPEN), with copyright held by Prophesee and its contributors. Any use, modification, or distribution of the openEB code must comply with the terms of the Apache License 2.0.

Third-party open source notices for openEB can be found at [OPEN_SOURCE_3RDPARTY_NOTICES](openeb/licensing/OPEN_SOURCE_3RDPARTY_NOTICES).
