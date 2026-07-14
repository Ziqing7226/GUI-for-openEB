// gui/algo_bridge/backends/openeb_filter_backends.cpp — RoiMask, AdaptiveRateSplit
// (design §3.4). Split from the former algo_backend.cpp monolith.

#include "algo_bridge/algo_backend.h"
#include "algo_bridge/backends/backend_common.h"

#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#include <metavision/sdk/core/algorithms/roi_mask_algorithm.h>
#include <metavision/sdk/core/algorithms/adaptive_rate_events_splitter_algorithm.h>

using namespace gui::backend_detail;

namespace gui {

// ===========================================================================
// OpenEB SDK — frame generators & filters (design §4.3.1 / §4.3.2)
//
// These backends wrap the real Metavision SDK algorithm classes. Events are
// stored in a std::vector<Metavision::EventCD> and the raw pointer is fed to
// the SDK process_events() (InputIt = Metavision::EventCD*).
// ===========================================================================

/// RoiMaskAlgorithm backend — propagates events inside a pixel mask.
/// The SDK operator() accesses the mask via cv::Mat::at<double>, so the mask
/// is kept in CV_64F to match.
class RoiMaskBackend final : public AlgoBackend {
    std::unique_ptr<Metavision::RoiMaskAlgorithm> algo_;
    std::vector<Metavision::EventCD> buf_;
    std::vector<Metavision::EventCD> out_;
    std::string mask_path_;
public:
    RoiMaskBackend(int w, int h) {
        cv::Mat mask = cv::Mat::ones(h, w, CV_64FC1); // all pixels pass
        algo_ = std::make_unique<Metavision::RoiMaskAlgorithm>(mask);
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "mask_path") {
            mask_path_ = v;
            if (!v.empty()) {
                cv::Mat m = cv::imread(v, cv::IMREAD_GRAYSCALE);
                if (!m.empty()) {
                    m.convertTo(m, CV_64F);
                    algo_->set_pixel_mask(m);
                }
            }
        }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "mask_path") return mask_path_;
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        buf_.assign(b, e);
        out_.clear();
        algo_->process_events(buf_.data(), buf_.data() + buf_.size(),
                              std::back_inserter(out_));
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = out_;
        r.status = "roi_mask: " + std::to_string(out_.size()) + "/" +
                   std::to_string(buf_.size()) +
                   (mask_path_.empty() ? "" : " (" + mask_path_ + ")");
        return r;
    }
    void reset() override { buf_.clear(); out_.clear(); }
};

/// AdaptiveRateEventsSplitterAlgorithm backend — splits the event stream into
/// sharp slices. retrieve_events() is called when a slice is ready.
class AdaptiveRateSplitBackend final : public AlgoBackend {
    int w_{0}, h_{0};
    float thr_{5e-4F};
    int downsample_{2};
    std::unique_ptr<Metavision::AdaptiveRateEventsSplitterAlgorithm> algo_;
    std::vector<Metavision::EventCD> buf_;
    std::vector<Metavision::EventCD> out_;
    int slices_{0};
public:
    AdaptiveRateSplitBackend(int w, int h) : w_(w), h_(h) { rebuild(); }
    void rebuild() {
        algo_ = std::make_unique<Metavision::AdaptiveRateEventsSplitterAlgorithm>(
            h_, w_, thr_, downsample_);
    }
    void set_param(const std::string& k, const std::string& v) override {
        bool dirty = false;
        if (k == "thr_var_per_event") { thr_ = static_cast<float>(to_d(v)); dirty = true; }
        else if (k == "downsampling_factor") { downsample_ = to_i(v); dirty = true; }
        if (dirty) rebuild();
    }
    std::string get_param(const std::string& k) const override {
        if (k == "thr_var_per_event") return from_d(thr_);
        if (k == "downsampling_factor") return from_i(downsample_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        buf_.assign(b, e);
        if (algo_->process_events(buf_.data(), buf_.data() + buf_.size())) {
            out_.clear();
            algo_->retrieve_events(out_);
            ++slices_;
        }
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = out_;
        r.status = "adaptive_rate_split: " + std::to_string(slices_) +
                   " slices, " + std::to_string(out_.size()) + " ev";
        return r;
    }
    void reset() override {
        if (algo_) algo_->retrieve_events(out_);
        buf_.clear(); out_.clear(); slices_ = 0;
    }
};


// --- Per-category factory (called by create_algo_backend in backend_factory.cpp)
std::unique_ptr<AlgoBackend> create_openeb_filter_backend(const std::string& name,
                                          int width, int height) {
    if (name == "roi_mask")                    return std::make_unique<RoiMaskBackend>(width, height);
    if (name == "adaptive_rate_split")         return std::make_unique<AdaptiveRateSplitBackend>(width, height);
    return nullptr;
}

} // namespace gui
