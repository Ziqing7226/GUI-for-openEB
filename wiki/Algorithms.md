# Algorithms

EB plus ships **59 algorithms** registered in a single `AlgoBridge` registry (`gui/algo_bridge/algo_bridge.cpp`):

- **29 self-developed** algorithms under `algo/` (21 Computer Vision + 8 Analytics). `intrinsic_calibration` was removed from the registry — it is now a **Tools → Intrinsic Wizard** dialog. Undistortion is available as a stackable preprocessing checkbox in the Algorithms panel (`preproc_undistort_enabled` / `preproc_undistort_path`).
- **30 OpenEB-wrapped** capabilities (10 filters + 7 frame modes + 7 preprocessors + 6 utilities)

Algorithms are **mutually exclusive** — enabling one disables the previous. Each self-developed algorithm supports a global ROI (default: center 128×128) and a shared preprocessing stage. All parameters are adjusted exclusively in the sidebar's **Algorithms** panel; algorithm display windows show only the title and output.

## Display Modes

Each algorithm declares a display mode that controls how its output reaches the screen:

| Mode | Behavior |
|------|----------|
| **Passive** | No direct display output (e.g. filters, analytics that log only) |
| **Overlay** | Draws annotations on top of the live event display (trajectories, vectors, boxes) |
| **Replace** | Replaces the event display with the algorithm's own frame |
| **Standalone** | Opens a separate `AlgoWindow` with its own output canvas |

## Shared Preprocessing

Every self-developed algorithm runs events through a stackable preprocessing stage, configured in the Algorithms panel:

```
ROI (default 128×128 center)  →  Noise Filter  →  1/4 Downsample
```

- **ROI** — bounds computational cost; default center 128×128.
- **Noise Filter** — 8 modes (see below), exposed based on the selected mode.
- **1/4 Downsample** — halves both dimensions (e.g. 128×128 → 64×64) before the algorithm runs; output is upsampled back.

These stages are **not** mutually exclusive with algorithms — they stack on top.

### Noise Filter Modes

Implemented in `algo/cv/noise_filter.h`. The GUI exposes parameters based on the selected mode:

| Mode | Description |
|------|-------------|
| BAF | Background Activity Filter |
| STCF | Spatio-Temporal Correlation Filter |
| Refractory | Refractory period filter |
| DWF | Directional Weighted Filter |
| AgePolarity | Age + polarity filter |
| Harmonic | Harmonic mean filter |
| Repetitious | Repetitious event filter |
| SpatialBP | Spatial Band-Pass filter |

## Self-Developed Algorithms

### Computer Vision (21)

| Algorithm | Display | Notes |
|-----------|---------|-------|
| Hot Pixel Filter | Passive | FPN correction option |
| Orientation Filter | Overlay | jAER SimpleOrientationFilter (min-dt WTA) |
| Direction Selective Filter | Overlay | jAER DirectionSelectiveFilter |
| Sparse Optical Flow | Overlay | 4 modes: LocalPlanes / LucasKanade / BlockMatch / ClusterOF |
| Blob Detector | Overlay | |
| Object Tracker | Overlay | 4 modes: RCT / Median / Kalman / MultiHypothesis |
| Corner Detector | Overlay | 3 modes: Harris / FAST / AGAST |
| Line Segment (ELiSeD) | Overlay | |
| Hough Line Tracker | Overlay | jAER HoughLineTracker |
| Hough Circle Tracker | Overlay | jAER HoughCircleTracker |
| Orientation Cluster | Overlay | |
| Cluster LIF | Overlay | LIF neuron clustering |
| Background Mask Filter | Replace | 2D histogram background modeling |
| Perspective Undistort | Passive | |
| Trigger Synced Filter | Passive | |
| Bandpass Filter | Overlay | |
| EIS (Optical Gyro) | Overlay | Electronic image stabilization |
| Ultra Slow Motion | Passive | Time dilation |
| XYT 3D Visualizer | Standalone | GPU 3D point cloud |
| Overlay | Overlay | Generic overlay |
| Time Surface | Standalone | Hot / Plasma / Turbo palettes |

### Analytics (7)

| Algorithm | Display | Notes |
|-----------|---------|-------|
| Active Marker Tracking | Overlay | Sliding-window clustering |
| Event -> Video (E2VID) | Standalone | 3 modes (see below) |
| Flow Statistics | Passive | Requires ground-truth |
| ISI Analyzer | Standalone | Inter-spike interval histograms |
| Particle Counter | Overlay | Line-crossing counter |
| Auto Bias Controller | Overlay | Closed-loop bias tuning |
| Frequency Detector | Standalone | Blinking frequency detection |

### Calibration (1)

| Algorithm | Display | Notes |
|-----------|---------|-------|
| Intrinsic Calibration | Standalone | chessboard / circle_grid / aruco |

## Event-to-Video (E2VID)

The Event-to-Video algorithm reconstructs grayscale intensity images from raw event streams. It has **3 modes**, selected via the `mode` parameter:

| Mode | Default | Description |
|------|---------|-------------|
| `0 = BardowVariational` | | Non-DL; joint optical-flow + log-intensity variational optimization |
| `1 = InteractingMaps` | | Non-DL; six interconnected maps (I/G/V/F/C/R) with rotation estimation |
| `2 = E2VID` | ✅ | DL; ONNX Runtime neural-network inference |

**Common parameters** (modes 0, 1): `output_fps` (1–120, default 30), `window_ms`, `decay_tau_ms` (0–5000, default 500).

### E2VID Setup

E2VID (default mode) requires a converted ONNX model. One-time setup (~5 minutes):

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

E2VID defaults to 128×128 ROI + 30 fps + 1/4 downsample (64×64 inference → upsampled to 128×128). `num_bins` is auto-determined by the ONNX model's input channel count when a model is loaded.

E2VID parameters exposed in the GUI: model path, `num_bins`, auto-HDR, unsharp amount/sigma, bilateral sigma.

> **Without ONNX Runtime**: E2VID falls back to a heuristic mode (voxel-grid sum + sigmoid). BardowVariational and InteractingMaps work without any extra setup.

### BardowVariational (mode 0)

Joint estimation of optical flow `u` and log-brightness `L` via Chambolle-Pock primal-dual optimization. Uses all six regularization terms:
- `lambda1` (flow TV), `lambda2` (temporal smoothness), `lambda3` (intensity TV)
- `lambda4` (flow constraint), `lambda5` (no-event dead zone), `lambda6` (prior map)

`lambda3` and `num_iterations` are shared with InteractingMaps.

### InteractingMaps (mode 1)

Six interconnected maps updated by alternating relaxation:
- **I** intensity, **G** gradient (= ∇I), **V** time-varying (−V = F·G), **F** optical flow, **C** calibration, **R** rotation (estimated via linear least squares).

V values are clamped to [-1, 1] to prevent NaN divergence. `I_map_` is reinitialized from Vc every frame to prevent ghosting.

## OpenEB-Wrapped Capabilities (30)

Registered in `AlgoBridge` under four categories — these wrap existing openEB SDK algorithms without reimplementation:

| Category | Count | Examples |
|----------|-------|----------|
| `openeb_filter` | 10 | FlipX, FlipY, ROI, Polarity, Event Rate Filter, … |
| `openeb_frame` | 7 | Integration, Diff, Histogram, Time Decay, Contrast Map, Periodic, On-Demand |
| `openeb_preproc` | 7 | Diff, Histo, Hardware Diff/Histo, Time Surface, Event Cube, Preprocessor Factory |
| `openeb_util` | 6 | Rate Estimator, Frame Composer, Rolling Buffer, Video Writer, Data Synchronizer, Timing Profiler |

## Adding a New Algorithm

1. Implement the algorithm in `algo/cv/` or `algo/analytics/` (header-only, operate on event packets).
2. Register it in `AlgoBridge::register_self_cv()` or `register_self_analytics()` with `AlgoInfo` (name, display name, category, display mode, parameters).
3. Implement the backend in `gui/algo_bridge/backends/` (e.g. `cv_backends.cpp`) — wire `set_param` / `get_param` / `process`.
4. If the backend has a factory entry, register it in `backend_registry.h`.
5. The algorithm auto-appears in the Algorithms panel; parameters are read/written via `set_param` / `get_param`.

All algorithms must have both `set_param` and `get_param` implementations so the GUI can read and write values.
