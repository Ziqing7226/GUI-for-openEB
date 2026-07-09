<div align="center">

# EBplus

A polished Qt 6 desktop app for event cameras — built on [openEB](https://github.com/prophesee-ai/openeb) v5.2.0.

Real-time visualization · camera control · recording · playback · calibration · 30+ algorithms · customizable themes

![License](https://img.shields.io/badge/license-MIT%20%2F%20Apache--2.0-blue)
![Language](https://img.shields.io/badge/C%2B%2B17-Qt%206-orange)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Version](https://img.shields.io/badge/version-1.0.0-blue)

![Main Window](pic/1.0.0.png)

</div>

---

## What is this?

**EBplus** is a GUI for event cameras (Prophesee / CenturyArks). Event cameras don't capture frames — they report per-pixel brightness changes at microsecond resolution. EBplus lets you:

- **See** the event stream in real time (OpenGL, 60+ FPS)
- **Control** the camera (biases, ROI, anti-flicker, triggers)
- **Record & replay** RAW event files
- **Run algorithms** — noise filtering, optical flow, object tracking, event-to-video, and more
- **Calibrate** the camera with a chessboard wizard
- **Export** to HDF5 / CSV / AVI

---

## Quick Start

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)

# Run (the launcher sets all required env vars)
./run.sh
```

That's it. The launcher handles Wayland compatibility, HAL plugin paths, and OpenGL backend selection automatically.

> **Requirements**: Ubuntu 22.04+ · GCC 13+ · Qt 6 · OpenCV 4. See [doc/compile.md](doc/compile.md) for details.

---

## Features

### Display
- OpenGL-accelerated rendering with letterboxed viewport
- 3 frame modes: Diff, Integration, Time Decay
- 4 color palettes: Dark, Light, CoolWarm, Gray
- Live stats: event rate, ON/OFF ratio, FPS, timestamp

### Camera Control
- **Biases** — all HAL biases with slider + spinbox, save/load `.bias` files
- **ROI** — drag-to-select on the display
- **ESP** — Anti-Flicker, Trail Filter, Event Rate Control
- **Trigger** — Trigger In (per-channel) + Trigger Out

### Recording & Playback
- RAW recording from live cameras
- File playback with speed control, seek, pause/resume
- File cutter — extract a time range

### Export & Conversion
- Convert between RAW, HDF5, and CSV
- Export to AVI video

### Algorithms (30+)
| Category | Examples |
|----------|----------|
| **Filtering** | Noise filter, refractory filter, spatial filter |
| **Motion** | Optical flow (HSV visualization), motion detection |
| **Detection** | Blob detector, Hough circle, particle counter |
| **Tracking** | KLT tracker, active marker tracking |
| **Reconstruction** | Event-to-video (E2VID default, BardowVariational, InteractingMaps) |
| **Analytics** | Frequency detector, flow statistics, ISI analyzer |
| **Calibration** | Intrinsic (chessboard), extrinsic |

Algorithms are **mutually exclusive** — enabling one disables the previous. Each algorithm supports a **global ROI** (default: center 256×256) to bound computational cost. All algorithm parameters are adjusted exclusively in the **sidebar** (AlgorithmsPanel); the algorithm display windows show only the algorithm title and output, preventing parameter drift between two independent control panels.

#### E2VID Neural Network Reconstruction (Default Mode)

The Event-to-Video algorithm defaults to **E2VID** — a deep-learning model that reconstructs grayscale images from raw event streams. It is ported from [rpg_e2vid](https://github.com/uzh-rpg/rpg_e2vid) and runs via ONNX Runtime (CPU, multi-threaded).

**Setup** (one-time, ~5 minutes):

```bash
# 1. Download ONNX Runtime 1.19.2 (Linux x64 CPU) into third_party/
cd /path/to/GUI-for-openEB
mkdir -p third_party/onnxruntime && cd third_party/onnxruntime
wget https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-linux-x64-1.19.2.tgz
tar xzf onnxruntime-linux-x64-1.19.2.tgz --strip-components=1
cd ../..

# 2. Create Python venv for model conversion
python3 -m venv .venv && . .venv/bin/activate
pip install torch --index-url https://download.pytorch.org/whl/cpu onnx onnxscript onnxruntime numpy
deactivate

# 3. Download pre-trained PyTorch weights (~41 MB)
wget -P models/ http://rpg.ifi.uzh.ch/data/E2VID/models/E2VID_lightweight.pth.tar

# 4. Convert to ONNX (produces models/e2vid_lightweight.onnx)
. .venv/bin/activate && python models/convert_to_onnx.py && deactivate

# 5. Rebuild (CMake auto-detects ONNX Runtime)
cmake --build build -- -j$(nproc)
```

After setup, launch EBplus and enable **Algorithm → Event → Video** — it defaults to E2VID mode with 128×128 ROI, 24 fps, and 1/4 downsample (64×64 inference → upsampled to 128×128). The GUI exposes toggleable parameters (model path, auto-HDR, downsample, unsharp mask, bilateral filter).

> **Without ONNX Runtime**: E2VID falls back to a heuristic mode (voxel-grid sum + sigmoid). BardowVariational and InteractingMaps modes work without any setup — BardowVariational jointly estimates optical flow and intensity via Chambolle-Pock primal-dual optimization (all six λ terms), and InteractingMaps uses six interconnected maps (I/G/V/F/C/R) with rotation estimation via least squares.

See [doc/design.md §4.4.2](doc/design.md) for full algorithm specifications.

### Theming
- **5 background colors**: Gray, Green, Yellow, Pink, Blue (default)
- **3 modes**: Follow System (default), Always Light, Always Dark
- Dark mode uses a **dark variant of the chosen color** — not just black
- Text color auto-adjusts (black on light, white on dark)
- Settings persist across restarts
- The menu bar follows the theme (native menu bar disabled)

### Multi-Window
- XYT 3D event point cloud (GPU-accelerated)
- Additional display windows (MDI)
- Save/restore dock layout to JSON

---

## Directory Structure

```
GUI-for-openEB/
├── gui/              # Qt 6 application
│   ├── main_window.*     # Main window: menus, docks, signal wiring
│   ├── display/          # OpenGL viewport, overlays, 3D cloud
│   ├── panels/           # Sidebar panels (biases, ROI, algorithms, …)
│   ├── app/              # Controllers (camera, pipeline, theme, …)
│   ├── algo_bridge/      # Algorithm registry + filter chain
│   ├── recorder/         # RAW recording & playback
│   ├── exporter/         # HDF5/CSV/AVI export
│   ├── calibration/      # Intrinsic wizard
│   └── widgets/          # AlgoWindow, multi-window manager
├── algo/              # Self-developed algorithm library
├── openeb/            # openEB SDK (Apache 2.0, v5.2.0)
├── run.sh             # Launcher (sets env vars)
├── doc/               # Design spec + build guide
└── pic/               # Screenshots
```

---

## Running

### Option 1: Launcher (Recommended)

```bash
./run.sh
```

The launcher auto-detects Wayland, forces XCB + OpenGL (avoids black screen), and sets HAL/HDF5 plugin paths.

### Option 2: Manual

```bash
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:/usr/local/lib"
export HDF5_PLUGIN_PATH="/usr/local/lib/hdf5/plugin"
export MV_HAL_PLUGIN_PATH=/usr/local/lib/metavision/hal/plugins  # Prophesee
# export MV_HAL_PLUGIN_PATH=/usr/lib/CenturyArks/hal/plugins     # CenturyArks
export QT_QPA_PLATFORM=xcb       # Wayland renders black for QOpenGLWidget
export QSG_RHI_BACKEND=opengl    # Qt 6 may default to Vulkan

./build/gui/gui_for_openeb
```

### Camera Vendor Paths

| Vendor | HAL plugin path |
|--------|----------------|
| Prophesee | `/usr/local/lib/metavision/hal/plugins` |
| CenturyArks | `/usr/lib/CenturyArks/hal/plugins` |

---

## Troubleshooting

**Black screen on startup** — Use the launcher script. If launching manually, set `QT_QPA_PLATFORM=xcb` and `QSG_RHI_BACKEND=opengl`.

**Camera not detected** — Verify `MV_HAL_PLUGIN_PATH` matches your vendor. Run `metavision_hal_ls` to check.

**"NonMonotonicTimeHigh" error** — This is a transient Evt3 protocol warning that occurs ~50% of the time on some Gen3.x cameras at startup. EBplus treats it as non-fatal and keeps the stream running. No action needed.

**Dark mode not following system** — Requires Qt 6.5+. On older Qt, use Theme → Mode → Dark.

---

## Known Issues & Feedback

EBplus is under active development and may still contain bugs. If you encounter any issue — crashes, rendering glitches, broken controls, or unexpected behavior — please [open an issue](../../issues). The project is continuously being improved, and bug reports from real users are the most direct help.

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+O` | Open file |
| `Ctrl+C` | Connect camera |
| `Ctrl+R` | ROI drag mode |
| `R` | Start recording |
| `F11` | Fullscreen |
| `Ctrl+Shift+S` | Toggle sidebar |
| `Ctrl+Q` | Quit |

---

## License

- **Original code**: [MIT](LICENSE)
- **openEB SDK**: [Apache 2.0](openeb/licensing/LICENSE_OPEN) — copyright Prophesee

---

<div align="center">

Built with Qt 6 · OpenCV · openEB SDK

</div>
