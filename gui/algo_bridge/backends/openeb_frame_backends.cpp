// gui/algo_bridge/backends/openeb_frame_backends.cpp — OpenEB SDK frame generators
// (design §3.4). Split from the former algo_backend.cpp monolith.

#include "algo_bridge/algo_backend.h"
#include "algo_bridge/backends/backend_common.h"

#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include <metavision/sdk/core/algorithms/events_integration_algorithm.h>
#include <metavision/sdk/core/algorithms/event_frame_diff_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/event_frame_histo_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/time_decay_frame_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/contrast_map_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/on_demand_frame_generation_algorithm.h>
#include <metavision/sdk/core/algorithms/base_frame_generation_algorithm.h>
#include <metavision/sdk/base/events/raw_event_frame_diff.h>
#include <metavision/sdk/base/events/raw_event_frame_histo.h>

using namespace gui::backend_detail;

namespace gui {

class FrameIntegrationBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    Metavision::timestamp decay_us_{1000000};
    std::unique_ptr<Metavision::EventsIntegrationAlgorithm> algo_;
    std::vector<Metavision::EventCD> passthrough_;
public:
    FrameIntegrationBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::EventsIntegrationAlgorithm>(
            static_cast<unsigned>(w_), static_cast<unsigned>(h_), decay_us_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "decay_time_us") {
            auto nv = static_cast<Metavision::timestamp>(to_i(v));
            if (nv != decay_us_) { decay_us_ = nv; rebuild(); }
        }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "decay_time_us") return from_i(static_cast<int>(decay_us_));
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        algo_->generate(r.frame);
        r.status = "frame_integration: decay=" + std::to_string(decay_us_) + "us";
        return r;
    }
    void reset() override { if (algo_) algo_->reset(); passthrough_.clear(); }
};

/// EventFrameDiffGenerationAlgorithm backend — diff event frame (sum of
/// polarities per pixel). bit_size / allow_rollover require reconstruction.
class FrameDiffBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    unsigned bit_size_{8};
    bool rollover_{true};
    std::unique_ptr<Metavision::EventFrameDiffGenerationAlgorithm<Metavision::EventCD*>> algo_;
    Metavision::RawEventFrameDiff frame_;
    std::vector<Metavision::EventCD> passthrough_;
public:
    FrameDiffBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::EventFrameDiffGenerationAlgorithm<Metavision::EventCD*>>(
            static_cast<unsigned>(w_), static_cast<unsigned>(h_), bit_size_, rollover_);
        frame_ = Metavision::RawEventFrameDiff(static_cast<unsigned>(h_),
                                               static_cast<unsigned>(w_), bit_size_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "bit_size") {
            int nv = to_i(v); if (nv >= 2 && nv <= 8) { bit_size_ = static_cast<unsigned>(nv); dirty = true; }
        } else if (k == "allow_rollover") { rollover_ = to_b(v); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "bit_size") return from_i(static_cast<int>(bit_size_));
        if (k == "allow_rollover") return from_b(rollover_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        algo_->generate(frame_);
        const auto& data = frame_.get_data();
        // int8 diff → CV_8U grayscale centred at 128 (0 = no activity)
        cv::Mat src(h_, w_, CV_8SC1,
                    const_cast<int8_t*>(data.data()));
        src.convertTo(r.frame, CV_8U, 1.0, 128.0);
        r.status = "frame_diff: " + std::to_string(passthrough_.size()) + " ev";
        return r;
    }
    void reset() override { if (algo_) algo_->reset(); passthrough_.clear(); }
};

/// EventFrameHistoGenerationAlgorithm backend — histo event frame (separate
/// neg/pos counts). channel_bit_neg/pos & packed require reconstruction.
class FrameHistoBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    unsigned bit_neg_{4}, bit_pos_{4};
    bool packed_{false};
    std::unique_ptr<Metavision::EventFrameHistoGenerationAlgorithm<Metavision::EventCD*>> algo_;
    Metavision::RawEventFrameHisto frame_;
    std::vector<Metavision::EventCD> passthrough_;
public:
    FrameHistoBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::EventFrameHistoGenerationAlgorithm<Metavision::EventCD*>>(
            static_cast<unsigned>(w_), static_cast<unsigned>(h_), bit_neg_, bit_pos_, packed_);
        frame_ = Metavision::RawEventFrameHisto(static_cast<unsigned>(h_),
                                                static_cast<unsigned>(w_),
                                                bit_neg_, bit_pos_, packed_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "channel_bit_neg") {
            int nv = to_i(v); if (nv >= 1 && nv <= 7) { bit_neg_ = static_cast<unsigned>(nv); dirty = true; }
        } else if (k == "channel_bit_pos") {
            int nv = to_i(v); if (nv >= 1 && nv <= 7) { bit_pos_ = static_cast<unsigned>(nv); dirty = true; }
        } else if (k == "packed") { packed_ = to_b(v); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "channel_bit_neg") return from_i(static_cast<int>(bit_neg_));
        if (k == "channel_bit_pos") return from_i(static_cast<int>(bit_pos_));
        if (k == "packed") return from_b(packed_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        algo_->generate(frame_);
        const auto& data = frame_.get_data();
        const int channels = packed_ ? 1 : 2;
        // Build a BGR frame: negative events → blue, positive events → red.
        r.frame = cv::Mat::zeros(h_, w_, CV_8UC3);
        if (channels == 2) {
            const int plane = w_ * h_;
            for (int y = 0; y < h_; ++y) {
                cv::Vec3b* row = r.frame.ptr<cv::Vec3b>(y);
                const int row_base = y * w_;
                for (int x = 0; x < w_; ++x) {
                    const int idx = row_base + x;
                    row[x][0] = data[idx];          // B = neg
                    row[x][2] = data[idx + plane];  // R = pos
                }
            }
        } else {
            cv::Mat src(h_, w_, CV_8UC1, const_cast<uint8_t*>(data.data()));
            cv::cvtColor(src, r.frame, cv::COLOR_GRAY2BGR);
        }
        r.status = "frame_histogram: " + std::to_string(passthrough_.size()) + " ev";
        return r;
    }
    void reset() override { if (algo_) algo_->reset(); passthrough_.clear(); }
};

/// TimeDecayFrameGenerationAlgorithm backend — time-decay visualization.
class FrameTimeDecayBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    std::unique_ptr<Metavision::TimeDecayFrameGenerationAlgorithm> algo_;
    std::vector<Metavision::EventCD> passthrough_;
    Metavision::timestamp decay_us_{100000};
    int palette_idx_{1}; // Dark
public:
    FrameTimeDecayBackend(int w, int h) : w_(w), h_(h) {
        algo_ = std::make_unique<Metavision::TimeDecayFrameGenerationAlgorithm>(
            w_, h_, decay_us_, to_palette(palette_idx_));
    }
    static Metavision::ColorPalette to_palette(int i) {
        switch (i) {
            case 0: return Metavision::ColorPalette::Light;
            case 2: return Metavision::ColorPalette::CoolWarm;
            case 3: return Metavision::ColorPalette::Gray;
            case 1: default: return Metavision::ColorPalette::Dark;
        }
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "exponential_decay_time_us") {
            decay_us_ = static_cast<Metavision::timestamp>(to_i(v));
            algo_->set_exponential_decay_time_us(decay_us_);
        } else if (k == "palette") {
            palette_idx_ = to_i(v);
            algo_->set_color_palette(to_palette(palette_idx_));
        }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "exponential_decay_time_us") return from_i(static_cast<int>(decay_us_));
        if (k == "palette") return from_i(palette_idx_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        algo_->generate(r.frame);
        r.status = "frame_time_decay: decay=" + std::to_string(decay_us_) + "us";
        return r;
    }
    void reset() override { if (algo_) algo_->reset(); passthrough_.clear(); }
};

/// ContrastMapGenerationAlgorithm backend — contrast map (log intensity).
class FrameContrastMapBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    float contrast_on_{1.2F};
    float contrast_off_{-1.0F};
    std::unique_ptr<Metavision::ContrastMapGenerationAlgorithm> algo_;
    std::vector<Metavision::EventCD> passthrough_;
public:
    FrameContrastMapBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::ContrastMapGenerationAlgorithm>(
            static_cast<unsigned>(w_), static_cast<unsigned>(h_), contrast_on_, contrast_off_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "contrast_on") { contrast_on_ = static_cast<float>(to_d(v)); dirty = true; }
        else if (k == "contrast_off") { contrast_off_ = static_cast<float>(to_d(v)); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "contrast_on") return from_d(contrast_on_);
        if (k == "contrast_off") return from_d(contrast_off_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        cv::Mat_<float> map;
        algo_->generate(map);
        // Normalize float contrast map to CV_8U for display.
        double mn, mx;
        cv::minMaxLoc(map, &mn, &mx);
        double range = (mx - mn > 1e-6) ? (mx - mn) : 1.0;
        map.convertTo(r.frame, CV_8U, 255.0 / range, -mn * 255.0 / range);
        cv::cvtColor(r.frame, r.frame, cv::COLOR_GRAY2BGR);
        r.status = "frame_contrast_map: " + std::to_string(passthrough_.size()) + " ev";
        return r;
    }
    void reset() override { if (algo_) algo_->reset(); passthrough_.clear(); }
};

/// PeriodicFrameGenerationAlgorithm backend — fixed-fps frame generation via
/// async callback. The callback stores the latest frame for pull_result().
class FramePeriodicBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    std::unique_ptr<Metavision::PeriodicFrameGenerationAlgorithm> algo_;
    std::vector<Metavision::EventCD> passthrough_;
    cv::Mat last_frame_;
    uint32_t accum_us_{10000};
    double fps_{30.0};
public:
    FramePeriodicBackend(int w, int h) : w_(w), h_(h) {
        algo_ = std::make_unique<Metavision::PeriodicFrameGenerationAlgorithm>(
            w_, h_, accum_us_, fps_);
        algo_->set_output_callback(
            [this](Metavision::timestamp, cv::Mat& frame) { last_frame_ = frame.clone(); });
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "accumulation_time_us") {
            accum_us_ = static_cast<uint32_t>(to_i(v));
            algo_->set_accumulation_time_us(accum_us_);
        } else if (k == "fps") {
            fps_ = to_d(v);
            algo_->set_fps(fps_);
        }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "accumulation_time_us") return from_i(static_cast<int>(accum_us_));
        if (k == "fps") return from_d(fps_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        if (!last_frame_.empty()) {
            r.has_frame = true;
            r.frame = last_frame_.clone();
        }
        r.status = "frame_periodic: " + from_d(fps_) + " fps, accum=" +
                   std::to_string(accum_us_) + "us";
        return r;
    }
    void reset() override {
        if (algo_) algo_->reset();
        passthrough_.clear(); last_frame_.release();
    }
};

/// OnDemandFrameGenerationAlgorithm backend — generates a frame at the last
/// event timestamp on each pull_result().
class FrameOnDemandBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    std::unique_ptr<Metavision::OnDemandFrameGenerationAlgorithm> algo_;
    std::vector<Metavision::EventCD> passthrough_;
    uint32_t accum_us_{0};
    Metavision::timestamp last_ts_{0};
public:
    FrameOnDemandBackend(int w, int h) : w_(w), h_(h) {
        algo_ = std::make_unique<Metavision::OnDemandFrameGenerationAlgorithm>(
            w_, h_, accum_us_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "accumulation_time_us") {
            accum_us_ = static_cast<uint32_t>(to_i(v));
            algo_->set_accumulation_time_us(accum_us_);
        }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "accumulation_time_us") return from_i(static_cast<int>(accum_us_));
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        if (!passthrough_.empty()) last_ts_ = passthrough_.back().t;
        algo_->process_events(passthrough_.data(), passthrough_.data() + passthrough_.size());
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.has_frame = true;
        algo_->generate(last_ts_, r.frame);
        r.status = "frame_on_demand: accum=" + std::to_string(accum_us_) +
                   "us, ts=" + std::to_string(last_ts_);
        return r;
    }
    void reset() override {
        if (algo_) algo_->reset();
        passthrough_.clear(); last_ts_ = 0;
    }
};


// --- Per-category factory (called by create_algo_backend in backend_factory.cpp)
std::unique_ptr<AlgoBackend> create_openeb_frame_backend(const std::string& name,
                                          int width, int height) {
    if (name == "frame_integration")           return std::make_unique<FrameIntegrationBackend>(width, height);
    if (name == "frame_diff")                  return std::make_unique<FrameDiffBackend>(width, height);
    if (name == "frame_histogram")             return std::make_unique<FrameHistoBackend>(width, height);
    if (name == "frame_time_decay")            return std::make_unique<FrameTimeDecayBackend>(width, height);
    if (name == "frame_contrast_map")          return std::make_unique<FrameContrastMapBackend>(width, height);
    if (name == "frame_periodic")              return std::make_unique<FramePeriodicBackend>(width, height);
    if (name == "frame_on_demand")             return std::make_unique<FrameOnDemandBackend>(width, height);
    return nullptr;
}

} // namespace gui
