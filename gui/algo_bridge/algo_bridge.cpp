// gui/algo_bridge/algo_bridge.cpp
//
// Phase 1: registry of all 46 algorithms (27 OpenEB capabilities + 19
// self-developed). create() returns a pass-through stub. Per-algorithm
// wiring is added in later phases.

#include "algo_bridge.h"

#include <mutex>

namespace gui {

// ---------------------------------------------------------------------------
// AlgoInstance
// ---------------------------------------------------------------------------

AlgoInstance::AlgoInstance(const AlgoInfo& info) : info_(info) {
    for (const auto& p : info_.params) {
        param_values_[p.key] = p.default_value;
    }
}

void AlgoInstance::set_param(const std::string& key, const std::string& value) {
    param_values_[key] = value;
}

std::string AlgoInstance::get_param(const std::string& key) const {
    auto it = param_values_.find(key);
    return it == param_values_.end() ? std::string{} : it->second;
}

void AlgoInstance::push_events(const Metavision::EventCD* begin,
                               const Metavision::EventCD* end) {
    if (!enabled_) {
        return;
    }
    // Phase 1 stub: pass-through copy.
    buffer_.insert(buffer_.end(), begin, end);
}

AlgoResult AlgoInstance::pull_result() {
    AlgoResult r;
    r.filtered_events = std::move(buffer_);
    r.status = "pass-through (phase 1 stub)";
    buffer_.clear();
    return r;
}

// ---------------------------------------------------------------------------
// Small spec helpers
// ---------------------------------------------------------------------------

namespace {

AlgoParamSpec pint(const std::string& k, const std::string& disp,
                   const std::string& def, const std::string& lo,
                   const std::string& hi) {
    return {k, disp, "int", def, lo, hi, {}};
}

AlgoParamSpec pfloat(const std::string& k, const std::string& disp,
                     const std::string& def, const std::string& lo,
                     const std::string& hi) {
    return {k, disp, "float", def, lo, hi, {}};
}

AlgoParamSpec penum(const std::string& k, const std::string& disp,
                    const std::string& def, std::vector<std::string> vals) {
    return {k, disp, "enum", def, "", "", std::move(vals)};
}

AlgoParamSpec pbool(const std::string& k, const std::string& disp,
                    const std::string& def) {
    return {k, disp, "bool", def, "", "", {}};
}

} // namespace

// ---------------------------------------------------------------------------
// AlgoBridge
// ---------------------------------------------------------------------------

AlgoBridge::AlgoBridge() {
    register_openeb_filters();
    register_openeb_frame_modes();
    register_openeb_preprocessors();
    register_openeb_utils();
    register_self_cv();
    register_self_analytics();
    register_self_calibration();
}

std::vector<AlgoInfo> AlgoBridge::list_algos() const {
    std::vector<AlgoInfo> out;
    out.reserve(registry_.size());
    for (const auto& kv : registry_) {
        out.push_back(kv.second);
    }
    return out;
}

const AlgoInfo* AlgoBridge::find(const std::string& name) const {
    auto it = registry_.find(name);
    return it == registry_.end() ? nullptr : &it->second;
}

std::shared_ptr<AlgoInstance> AlgoBridge::create(const std::string& name) {
    auto it = registry_.find(name);
    if (it == registry_.end()) {
        return nullptr;
    }
    return std::make_shared<AlgoInstance>(it->second);
}

void AlgoBridge::push_events(const std::shared_ptr<AlgoInstance>& inst,
                             const Metavision::EventCD* begin,
                             const Metavision::EventCD* end) {
    if (inst) {
        inst->push_events(begin, end);
    }
}

AlgoResult AlgoBridge::pull_result(const std::shared_ptr<AlgoInstance>& inst) {
    if (!inst) {
        return {};
    }
    return inst->pull_result();
}

// ---------------------------------------------------------------------------
// OpenEB-wrapped filters (design §4.3.1)
// ---------------------------------------------------------------------------

void AlgoBridge::register_openeb_filters() {
    auto add = [&](AlgoInfo a) {
        a.source = "openeb";
        a.category = "openeb_filter";
        registry_[a.name] = std::move(a);
    };

    add({"roi_filter", "ROI Filter", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive,
         {pint("x0", "X start", "0", "0", ""),
          pint("y0", "Y start", "0", "0", ""),
          pint("x1", "X end", "1279", "0", ""),
          pint("y1", "Y end", "719", "0", ""),
          pbool("output_relative_coordinates", "Relative coords", "false")}});

    add({"roi_mask", "ROI Mask", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive,
         {{"mask_path", "Mask image path", "string", "", "", "", {}}}});

    add({"polarity_filter", "Polarity Filter", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive,
         {penum("polarity", "Polarity", "1", {"0=OFF", "1=ON"})}});

    add({"polarity_invert", "Polarity Invert", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive, {}});

    add({"flip_x", "Flip X", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive, {}});

    add({"flip_y", "Flip Y", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive, {}});

    add({"rotate", "Rotate", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive,
         {penum("rotation", "Rotation (deg)", "0", {"0", "90", "180", "270"})}});

    add({"transpose", "Transpose", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive, {}});

    add({"rescale", "Rescale", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive,
         {pfloat("scale_width", "Scale X", "1.0", "0.0001", "10"),
          pfloat("scale_height", "Scale Y", "1.0", "0.0001", "10")}});

    add({"adaptive_rate_split", "Adaptive Rate Split", "openeb_filter", "openeb",
         AlgoDisplayMode::Passive,
         {pfloat("thr_var_per_event", "Var threshold", "5e-4", "1e-5", "1e-2"),
          pint("downsampling_factor", "Downsampling", "2", "1", "8")}});
}

// ---------------------------------------------------------------------------
// OpenEB-wrapped frame generators (design §4.3.2)
// ---------------------------------------------------------------------------

void AlgoBridge::register_openeb_frame_modes() {
    auto add = [&](AlgoInfo a) {
        a.source = "openeb";
        a.category = "openeb_frame";
        a.display_mode = AlgoDisplayMode::Replace;
        registry_[a.name] = std::move(a);
    };

    add({"frame_integration", "Integration Frame", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {pint("decay_time_us", "Decay time (us)", "1000000", "10000", "10000000")}});

    add({"frame_diff", "Diff Frame", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"frame_histogram", "Histogram Frame", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"frame_time_decay", "Time Decay Frame", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {pint("exponential_decay_time_us", "Decay (us)", "100000", "10000", "10000000")}});

    add({"frame_contrast_map", "Contrast Map", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"frame_periodic", "Periodic Frame", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {pint("period_us", "Period (us)", "33000", "1000", "1000000")}});

    add({"frame_on_demand", "On-Demand Frame", "openeb_frame", "openeb",
         AlgoDisplayMode::Replace,
         {penum("trigger", "Trigger strategy", "N_EVENTS",
                {"N_EVENTS", "N_US", "MIXED"})}});
}

// ---------------------------------------------------------------------------
// OpenEB-wrapped preprocessors (design §4.3.3)
// ---------------------------------------------------------------------------

void AlgoBridge::register_openeb_preprocessors() {
    auto add = [&](AlgoInfo a) {
        a.source = "openeb";
        a.category = "openeb_preproc";
        a.display_mode = AlgoDisplayMode::Passive;
        registry_[a.name] = std::move(a);
    };

    add({"preproc_diff", "Diff Preprocessor", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive,
         {pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"preproc_histo", "Histo Preprocessor", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive,
         {pint("max_events_per_pixel", "Max events/px", "5", "1", "255"),
          pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"preproc_hw_diff", "Hardware Diff", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive,
         {pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"preproc_hw_histo", "Hardware Histo", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive, {}});

    add({"preproc_time_surface", "Time Surface Processor", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive,
         {penum("channels", "Channels", "1", {"1=merged", "2=split"})}});

    add({"preproc_event_cube", "Event Cube", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive,
         {pint("num_bins", "Num bins", "10", "2", "20"),
          pint("accumulation_time_us", "Accumulation (us)", "33000", "1000", "1000000")}});

    add({"preproc_factory", "Preprocessor Factory", "openeb_preproc", "openeb",
         AlgoDisplayMode::Passive,
         {{"config_path", "JSON config path", "string", "", "", "", {}}}});
}

// ---------------------------------------------------------------------------
// OpenEB-wrapped utilities (design §4.3.4)
// ---------------------------------------------------------------------------

void AlgoBridge::register_openeb_utils() {
    auto add = [&](AlgoInfo a) {
        a.source = "openeb";
        a.category = "openeb_util";
        a.display_mode = AlgoDisplayMode::Passive;
        registry_[a.name] = std::move(a);
    };

    add({"util_rate_estimator", "Rate Estimator", "openeb_util", "openeb",
         AlgoDisplayMode::Passive, {}});
    add({"util_frame_composer", "Frame Composer", "openeb_util", "openeb",
         AlgoDisplayMode::Passive, {}});
    add({"util_rolling_buffer", "Rolling Buffer", "openeb_util", "openeb",
         AlgoDisplayMode::Passive, {}});
    add({"util_video_writer", "Video Writer", "openeb_util", "openeb",
         AlgoDisplayMode::Passive, {}});
    add({"util_data_synchronizer", "Data Synchronizer", "openeb_util", "openeb",
         AlgoDisplayMode::Passive, {}});
    add({"util_timing_profiler", "Timing Profiler", "openeb_util", "openeb",
         AlgoDisplayMode::Passive, {}});
}

// ---------------------------------------------------------------------------
// Self-developed CV algorithms (design §4.3.5 - §4.3.15)
// ---------------------------------------------------------------------------

void AlgoBridge::register_self_cv() {
    auto add = [&](AlgoInfo a) {
        a.source = "self";
        a.category = "cv";
        registry_[a.name] = std::move(a);
    };

    add({"noise_filter", "Noise Filter", "cv", "self",
         AlgoDisplayMode::Overlay,
         {pint("time_window_us", "Time window (us)", "5000", "1000", "100000"),
          pint("spatial_radius_px", "Spatial radius (px)", "5", "1", "20"),
          pint("min_neighbors", "Min neighbors", "2", "1", "10")}});

    add({"sparse_optical_flow", "Sparse Optical Flow", "cv", "self",
         AlgoDisplayMode::Overlay,
         {pint("time_window_us", "Time window (us)", "10000", "1000", "100000"),
          pint("spatial_radius_px", "Spatial radius (px)", "8", "3", "30"),
          pint("min_events_per_cluster", "Min events/cluster", "10", "3", "100")}});

    add({"dense_optical_flow", "Dense Optical Flow", "cv", "self",
         AlgoDisplayMode::Overlay,
         {pint("block_size", "Block size", "16", "4", "64"),
          pint("step", "Step", "8", "1", "32"),
          pint("time_window_us", "Time window (us)", "10000", "1000", "100000")}});

    add({"blob_detector", "Blob Detector", "cv", "self",
         AlgoDisplayMode::Overlay,
         {pfloat("accumulation_ms", "Accumulation (ms)", "33.3", "1", "1000"),
          pint("threshold", "Threshold", "50", "1", "254"),
          pint("min_area", "Min area (px)", "10", "1", "100000")}});

    add({"object_tracker", "Object Tracker", "cv", "self",
         AlgoDisplayMode::Overlay,
         {pint("cluster_time_us", "Cluster time (us)", "5000", "1000", "50000"),
          pint("cluster_radius_px", "Cluster radius (px)", "10", "3", "50"),
          pint("min_cluster_events", "Min cluster events", "50", "10", "500"),
          pfloat("max_lost_age_s", "Max lost age (s)", "1.0", "0.1", "5.0")}});

    add({"corner_detector", "Corner Detector", "cv", "self",
         AlgoDisplayMode::Overlay,
         {pfloat("accumulation_ms", "Accumulation (ms)", "10", "1", "100"),
          pfloat("harris_threshold", "Harris threshold", "0.01", "0", "0.1"),
          pint("track_radius_px", "Track radius (px)", "5", "1", "30"),
          pint("min_track_len", "Min track length", "10", "1", "100"),
          pint("output_hz", "Output rate (Hz)", "100", "10", "500")}});

    add({"counter", "Counter", "cv", "self",
         AlgoDisplayMode::Overlay,
         {penum("direction", "Direction", "bidirectional",
                {"bidirectional", "forward", "reverse"}),
          pfloat("debounce_ms", "Debounce (ms)", "100", "0", "1000")}});

    add({"ultra_slow_motion", "Ultra Slow Motion", "cv", "self",
         AlgoDisplayMode::Replace,
         {pfloat("dilation_factor", "Dilation factor", "10", "1", "10000"),
          pint("min_accumulation_us", "Min accumulation (us)", "5", "1", "1000")}});

    add({"xyt_visualizer", "XYT Visualizer", "cv", "self",
         AlgoDisplayMode::Standalone,
         {penum("axis", "Axis", "X", {"X", "Y"}),
          pint("line_position_px", "Line position (px)", "0", "0", ""),
          pfloat("time_window_ms", "Time window (ms)", "1000", "10", "10000"),
          pfloat("accumulation_ms", "Accumulation (ms)", "100", "1", "1000")}});

    add({"time_surface_window", "Time Surface Window", "cv", "self",
         AlgoDisplayMode::Standalone,
         {penum("channels", "Channels", "1", {"1=merged", "2=split"}),
          pint("decay_time_us", "Decay time (us)", "100000", "10000", "5000000"),
          penum("palette", "Palette", "Hot", {"Gray", "Hot", "Plasma", "Turbo"}),
          pint("refresh_rate_hz", "Refresh (Hz)", "30", "10", "120")}});

    add({"stereo_matcher", "Stereo Matcher", "cv", "self",
         AlgoDisplayMode::Standalone,
         {pint("disparity_range", "Disparity range", "64", "16", "256"),
          pint("block_size", "Block size", "7", "3", "21")}});

    add({"overlay", "Overlay", "cv", "self",
         AlgoDisplayMode::Overlay, {}});
}

// ---------------------------------------------------------------------------
// Self-developed analytics (design §4.4)
// ---------------------------------------------------------------------------

void AlgoBridge::register_self_analytics() {
    auto add = [&](AlgoInfo a) {
        a.source = "self";
        a.category = "analytics";
        registry_[a.name] = std::move(a);
    };

    add({"active_marker", "Active Marker Tracking", "analytics", "self",
         AlgoDisplayMode::Overlay,
         {pint("min_frequency_hz", "Min frequency (Hz)", "100", "1", "10000"),
          pint("max_frequency_hz", "Max frequency (Hz)", "5000", "1", "100000")}});

    add({"event_to_video", "Event -> Video (E2VID)", "analytics", "self",
         AlgoDisplayMode::Standalone,
         {{"model_path", "Model path", "string", "", "", "", {}},
          pint("output_fps", "Output FPS", "30", "1", "120"),
          pfloat("accumulation_ms", "Accumulation (ms)", "33.3", "1", "1000")}});
}

// ---------------------------------------------------------------------------
// Self-developed calibration (design §4.5)
// ---------------------------------------------------------------------------

void AlgoBridge::register_self_calibration() {
    auto add = [&](AlgoInfo a) {
        a.source = "self";
        a.category = "calibration";
        a.display_mode = AlgoDisplayMode::Standalone;
        registry_[a.name] = std::move(a);
    };

    add({"intrinsic_calibration", "Intrinsic Calibration", "calibration", "self",
         AlgoDisplayMode::Standalone,
         {penum("board_type", "Board type", "chessboard",
                {"chessboard", "circle_grid", "aruco"}),
          pint("squares_x", "Squares X", "9", "2", "30"),
          pint("squares_y", "Squares Y", "6", "2", "30"),
          pfloat("square_size_mm", "Square size (mm)", "25", "1", "200")}});

    add({"extrinsic_calibration", "Extrinsic Calibration", "calibration", "self",
         AlgoDisplayMode::Standalone,
         {pint("num_cameras", "Num cameras", "2", "2", "8")}});
}

} // namespace gui
