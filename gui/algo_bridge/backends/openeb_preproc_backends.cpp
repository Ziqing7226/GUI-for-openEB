// gui/algo_bridge/backends/openeb_preproc_backends.cpp — OpenEB SDK preprocessors
// (design §3.4). Split from the former algo_backend.cpp monolith.

#include "algo_bridge/algo_backend.h"
#include "algo_bridge/backends/backend_common.h"

#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include <metavision/sdk/core/preprocessors/diff_processor.h>
#include <metavision/sdk/core/preprocessors/histo_processor.h>
#include <metavision/sdk/core/preprocessors/time_surface_processor.h>
#include <metavision/sdk/core/preprocessors/event_cube_processor.h>
#include <metavision/sdk/core/preprocessors/tensor.h>

using namespace gui::backend_detail;

namespace gui {

// ===========================================================================
// OpenEB SDK — preprocessors (design §4.3.3)
//
// Preprocessors fill a Tensor (float). The tensor is accumulated across
// push_events() calls and reset after each pull_result() so each displayed
// frame shows activity since the last pull. The Tensor is converted to a
// cv::Mat for visualization.
// ===========================================================================

/// DiffProcessor backend — single-channel diff tensor → grayscale frame.
class PreprocDiffBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    float max_incr_{5.0F};
    float clip_{1.0F};
    std::unique_ptr<Metavision::DiffProcessor<Metavision::EventCD*>> algo_;
    Metavision::Tensor tensor_;
    Metavision::timestamp frame_start_ts_{-1};
    std::vector<Metavision::EventCD> passthrough_;
public:
    PreprocDiffBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::DiffProcessor<Metavision::EventCD*>>(
            w_, h_, max_incr_, clip_);
        tensor_.create(algo_->get_output_shape(), algo_->get_output_type());
        tensor_.set_to(0.f);
        frame_start_ts_ = -1;
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "max_incr_per_pixel") { max_incr_ = static_cast<float>(to_d(v)); dirty = true; }
        else if (k == "clip_value_after_normalization") { clip_ = static_cast<float>(to_d(v)); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "max_incr_per_pixel") return from_d(max_incr_);
        if (k == "clip_value_after_normalization") return from_d(clip_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        if (passthrough_.empty()) return;
        if (frame_start_ts_ < 0) frame_start_ts_ = passthrough_.front().t;
        algo_->process_events(frame_start_ts_, passthrough_.data(),
                              passthrough_.data() + passthrough_.size(), tensor_);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        const float* d = tensor_.data<float>();
        // Tensor layout CHW with C=1 → contiguous HxW float plane.
        cv::Mat src(h_, w_, CV_32F, const_cast<float*>(d));
        // Values in [-clip, clip] → map to [0, 255] centred at 128.
        double scale = (clip_ > 0) ? (127.0 / clip_) : 127.0;
        src.convertTo(r.frame, CV_8U, scale, 128.0);
        cv::cvtColor(r.frame, r.frame, cv::COLOR_GRAY2BGR);
        r.status = "preproc_diff: max_incr=" + from_d(max_incr_) + " clip=" + from_d(clip_);
        // Reset accumulation for the next frame.
        tensor_.set_to(0.f);
        frame_start_ts_ = -1;
        return r;
    }
    void reset() override { tensor_.set_to(0.f); frame_start_ts_ = -1; passthrough_.clear(); }
};

/// HistoProcessor backend — two-channel histogram tensor → BGR frame.
class PreprocHistoBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    float max_incr_{5.0F};
    float clip_{1.0F};
    bool use_chw_{true};
    std::unique_ptr<Metavision::HistoProcessor<Metavision::EventCD*>> algo_;
    Metavision::Tensor tensor_;
    Metavision::timestamp frame_start_ts_{-1};
    std::vector<Metavision::EventCD> passthrough_;
public:
    PreprocHistoBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::HistoProcessor<Metavision::EventCD*>>(
            w_, h_, max_incr_, clip_, use_chw_);
        tensor_.create(algo_->get_output_shape(), algo_->get_output_type());
        tensor_.set_to(0.f);
        frame_start_ts_ = -1;
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "max_incr_per_pixel") { max_incr_ = static_cast<float>(to_d(v)); dirty = true; }
        else if (k == "clip_value_after_normalization") { clip_ = static_cast<float>(to_d(v)); dirty = true; }
        else if (k == "use_CHW") { use_chw_ = to_b(v); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "max_incr_per_pixel") return from_d(max_incr_);
        if (k == "clip_value_after_normalization") return from_d(clip_);
        if (k == "use_CHW") return from_b(use_chw_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        if (passthrough_.empty()) return;
        if (frame_start_ts_ < 0) frame_start_ts_ = passthrough_.front().t;
        algo_->process_events(frame_start_ts_, passthrough_.data(),
                              passthrough_.data() + passthrough_.size(), tensor_);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        const float* d = tensor_.data<float>();
        const int plane = w_ * h_;
        r.frame = cv::Mat::zeros(h_, w_, CV_8UC3);
        double scale = (clip_ > 0) ? (255.0 / clip_) : 255.0;
        // Channel 0 = negative, channel 1 = positive (CHW: c-plane; HWC: interleaved).
        for (int y = 0; y < h_; ++y) {
            cv::Vec3b* row = r.frame.ptr<cv::Vec3b>(y);
            const int row_base = y * w_;
            for (int x = 0; x < w_; ++x) {
                const int idx = row_base + x;
                float neg, pos;
                if (use_chw_) { neg = d[idx]; pos = d[idx + plane]; }
                else { neg = d[idx * 2]; pos = d[idx * 2 + 1]; }
                row[x][0] = static_cast<uint8_t>(
                    std::min(255.0, std::max(0.0, neg * scale)));
                row[x][2] = static_cast<uint8_t>(
                    std::min(255.0, std::max(0.0, pos * scale)));
            }
        }
        r.status = "preproc_histo: CHW=" + from_b(use_chw_);
        tensor_.set_to(0.f);
        frame_start_ts_ = -1;
        return r;
    }
    void reset() override { tensor_.set_to(0.f); frame_start_ts_ = -1; passthrough_.clear(); }
};

/// TimeSurfaceProcessor backend — most-recent-timestamp buffer → time surface
/// image. The CHANNELS template parameter (1 or 2) is selected at runtime;
/// changing it reconstructs the processor.
class PreprocTimeSurfaceBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    int channels_{1};
    std::unique_ptr<Metavision::TimeSurfaceProcessor<Metavision::EventCD*, 1>> ts1_;
    std::unique_ptr<Metavision::TimeSurfaceProcessor<Metavision::EventCD*, 2>> ts2_;
    Metavision::MostRecentTimestampBuffer buf_;
    std::vector<Metavision::EventCD> passthrough_;
    Metavision::timestamp last_ts_{0};
public:
    PreprocTimeSurfaceBackend(int w, int h) : w_(w), h_(h) {
        buf_.create(h_, w_, 1);
        rebuild();
    }
    void rebuild() {
        ts1_.reset();
        ts2_.reset();
        buf_.create(h_, w_, channels_);
        buf_.set_to(0);
        if (channels_ == 1) ts1_ = std::make_unique<Metavision::TimeSurfaceProcessor<Metavision::EventCD*, 1>>(w_, h_);
        else ts2_ = std::make_unique<Metavision::TimeSurfaceProcessor<Metavision::EventCD*, 2>>(w_, h_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "channels") {
            int c = to_i(v);
            if (c == 1 || c == 2) { channels_ = c; rebuild(); }
        }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "channels") return from_i(channels_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        if (passthrough_.empty()) return;
        last_ts_ = passthrough_.back().t;
        if (channels_ == 1) ts1_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size(), buf_);
        else ts2_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size(), buf_);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        // Decay window: 100 ms by default for visualization.
        const Metavision::timestamp delta_t = 100000;
        if (channels_ == 2) buf_.generate_img_time_surface(last_ts_, delta_t, r.frame);
        else buf_.generate_img_time_surface_collapsing_channels(last_ts_, delta_t, r.frame);
        cv::cvtColor(r.frame, r.frame, cv::COLOR_GRAY2BGR);
        r.status = "preproc_time_surface: channels=" + from_i(channels_);
        return r;
    }
    void reset() override { buf_.set_to(0); passthrough_.clear(); last_ts_ = 0; }
};

/// EventCubeProcessor backend — event-cube tensor → grayscale projection.
class PreprocEventCubeBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    int num_bins_{10};
    Metavision::timestamp delta_t_us_{33000};
    bool split_polarity_{false};
    float max_incr_{5.0F};
    std::unique_ptr<Metavision::EventCubeProcessor<Metavision::EventCD*>> algo_;
    Metavision::Tensor tensor_;
    Metavision::timestamp frame_start_ts_{-1};
    std::vector<Metavision::EventCD> passthrough_;
public:
    PreprocEventCubeBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::EventCubeProcessor<Metavision::EventCD*>>(
            delta_t_us_, w_, h_, num_bins_, split_polarity_, max_incr_);
        tensor_.create(algo_->get_output_shape(), algo_->get_output_type());
        tensor_.set_to(0.f);
        frame_start_ts_ = -1;
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "num_bins") { int n = to_i(v); if (n >= 2 && n <= 20) { num_bins_ = n; dirty = true; } }
        else if (k == "delta_t_us") { delta_t_us_ = static_cast<Metavision::timestamp>(to_i(v)); dirty = true; }
        else if (k == "split_polarity") { split_polarity_ = to_b(v); dirty = true; }
        else if (k == "max_incr_per_pixel") { max_incr_ = static_cast<float>(to_d(v)); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "num_bins") return from_i(num_bins_);
        if (k == "delta_t_us") return from_i(static_cast<int>(delta_t_us_));
        if (k == "split_polarity") return from_b(split_polarity_);
        if (k == "max_incr_per_pixel") return from_d(max_incr_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        if (passthrough_.empty()) return;
        if (frame_start_ts_ < 0) frame_start_ts_ = passthrough_.front().t;
        algo_->process_events(frame_start_ts_, passthrough_.data(),
                              passthrough_.data() + passthrough_.size(), tensor_);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        const float* d = tensor_.data<float>();
        const int plane = w_ * h_;
        const int c = static_cast<int>(tensor_.shape().get_nb_values()) / plane;
        // Project the cube across channels into a single HxW plane.
        cv::Mat acc = cv::Mat::zeros(h_, w_, CV_32F);
        for (int ch = 0; ch < c; ++ch) {
            const float* dch = d + ch * plane;
            for (int y = 0; y < h_; ++y) {
                float* row = acc.ptr<float>(y);
                const int row_base = y * w_;
                for (int x = 0; x < w_; ++x) {
                    row[x] += dch[row_base + x];
                }
            }
        }
        double mn, mx;
        cv::minMaxLoc(acc, &mn, &mx);
        double range = (mx - mn > 1e-6) ? (mx - mn) : 1.0;
        acc.convertTo(r.frame, CV_8U, 255.0 / range, -mn * 255.0 / range);
        cv::cvtColor(r.frame, r.frame, cv::COLOR_GRAY2BGR);
        r.status = "preproc_event_cube: bins=" + from_i(num_bins_) +
                   " dt=" + std::to_string(delta_t_us_) + "us";
        tensor_.set_to(0.f);
        frame_start_ts_ = -1;
        return r;
    }
    void reset() override { tensor_.set_to(0.f); frame_start_ts_ = -1; passthrough_.clear(); }
};

/// EventPreprocessorFactory backend — stub. The factory requires a JSON config
/// parser; until a config_path is provided, this is a pass-through with an
/// informative status.
class PreprocFactoryBackend final : public AlgoBackend {
    std::vector<Metavision::EventCD> passthrough_;
    std::string config_path_;
public:
    PreprocFactoryBackend(int, int) {}
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "config_path") config_path_ = v;
    }
    std::string get_param(const std::string& k) const override {
        if (k == "config_path") return config_path_;
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "preproc_factory: stub — set config_path to load JSON" +
                   (config_path_.empty() ? "" : " (" + config_path_ + ")");
        return r;
    }
    void reset() override { passthrough_.clear(); }
};


// --- Per-category factory (called by create_algo_backend in backend_factory.cpp)
std::unique_ptr<AlgoBackend> create_openeb_preproc_backend(const std::string& name,
                                          int width, int height) {
    if (name == "preproc_diff")                return std::make_unique<PreprocDiffBackend>(width, height);
    if (name == "preproc_histo")               return std::make_unique<PreprocHistoBackend>(width, height);
    if (name == "preproc_time_surface")        return std::make_unique<PreprocTimeSurfaceBackend>(width, height);
    if (name == "preproc_event_cube")          return std::make_unique<PreprocEventCubeBackend>(width, height);
    if (name == "preproc_factory")             return std::make_unique<PreprocFactoryBackend>(width, height);
    return nullptr;
}

} // namespace gui
