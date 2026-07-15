// gui/algo_bridge/backends/openeb_util_backends.cpp — OpenEB SDK utilities
// (design §3.4). Split from the former algo_backend.cpp monolith.

#include "algo_bridge/algo_backend.h"
#include "algo_bridge/backends/backend_common.h"

#include <string>
#include <vector>

#include <metavision/sdk/core/utils/frame_composer.h>
#include <metavision/sdk/core/utils/rolling_event_buffer.h>
#include <metavision/sdk/core/utils/mostrecent_timestamp_buffer.h>

using namespace gui::backend_detail;

namespace gui {

// ===========================================================================
// OpenEB SDK — utilities (design §4.3.4)
//
// Most utilities are passive containers / recorders with no per-event frame
// output. They pass events through and report an informative status. The
// RollingEventBuffer is implemented fully.
// ===========================================================================

/// FrameComposer backend — passive container (status only).
class UtilFrameComposerBackend final : public AlgoBackend {
    std::vector<Metavision::EventCD> passthrough_;
public:
    UtilFrameComposerBackend(int, int) {}
    void set_param(const std::string&, const std::string&) override {}
    std::string get_param(const std::string&) const override { return {}; }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "frame_composer: ready — use API to add subimages";
        return r;
    }
    void reset() override { passthrough_.clear(); }
};

/// RollingEventBuffer backend — keeps a rolling window of events (by count or
/// duration) and passes them through.
class UtilRollingBufferBackend final : public AlgoBackend {
    int mode_{0}; // 0=N_EVENTS, 1=N_US
    Metavision::timestamp delta_ts_{1000000};
    std::size_t delta_n_{5000};
    Metavision::RollingEventBuffer<Metavision::EventCD> buf_;
    std::vector<Metavision::EventCD> passthrough_;
public:
    UtilRollingBufferBackend(int, int) {
        buf_ = Metavision::RollingEventBuffer<Metavision::EventCD>(
            Metavision::RollingEventBufferConfig::make_n_events(delta_n_));
    }
    void rebuild() {
        if (mode_ == 1)
            buf_ = Metavision::RollingEventBuffer<Metavision::EventCD>(
                Metavision::RollingEventBufferConfig::make_n_us(delta_ts_));
        else
            buf_ = Metavision::RollingEventBuffer<Metavision::EventCD>(
                Metavision::RollingEventBufferConfig::make_n_events(delta_n_));
    }
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "mode") { mode_ = to_i(v); rebuild(); }
        else if (k == "delta_n_events") { delta_n_ = static_cast<std::size_t>(to_i(v)); rebuild(); }
        else if (k == "delta_ts_us") { delta_ts_ = static_cast<Metavision::timestamp>(to_i(v)); rebuild(); }
    }
    std::string get_param(const std::string& k) const override {
        if (k == "mode") return from_i(mode_);
        if (k == "delta_n_events") return from_i(static_cast<int>(delta_n_));
        if (k == "delta_ts_us") return from_i(static_cast<int>(delta_ts_));
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
        buf_.insert_events(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "rolling_buffer: " + std::to_string(buf_.size()) + "/" +
                   std::to_string(buf_.capacity()) + " ev" +
                   (mode_ == 1 ? " (N_US)" : " (N_EVENTS)");
        return r;
    }
    void reset() override { buf_.clear(); passthrough_.clear(); }
};

/// DataSynchronizerFromTriggers backend — stub (complex API).
class UtilDataSynchronizerBackend final : public AlgoBackend {
    std::vector<Metavision::EventCD> passthrough_;
    int period_us_{10000};
public:
    UtilDataSynchronizerBackend(int, int) {}
    void set_param(const std::string& k, const std::string& v) override {
        if (k == "period_us") period_us_ = to_i(v);
    }
    std::string get_param(const std::string& k) const override {
        if (k == "period_us") return from_i(period_us_);
        return {};
    }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "data_synchronizer: ready — period_us=" + std::to_string(period_us_);
        return r;
    }
    void reset() override { passthrough_.clear(); }
};

/// TimingProfiler backend — stub (singleton, no per-event output).
class UtilTimingProfilerBackend final : public AlgoBackend {
    std::vector<Metavision::EventCD> passthrough_;
public:
    UtilTimingProfilerBackend(int, int) {}
    void set_param(const std::string&, const std::string&) override {}
    std::string get_param(const std::string&) const override { return {}; }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "timing_profiler: active";
        return r;
    }
    void reset() override { passthrough_.clear(); }
};

/// Rate estimator backend — stub (see Statistics panel).
class UtilRateEstimatorBackend final : public AlgoBackend {
    std::vector<Metavision::EventCD> passthrough_;
public:
    UtilRateEstimatorBackend(int, int) {}
    void set_param(const std::string&, const std::string&) override {}
    std::string get_param(const std::string&) const override { return {}; }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "rate_estimator: see Statistics panel";
        return r;
    }
    void reset() override { passthrough_.clear(); }
};

/// Video writer backend — stub (use Export menu).
class UtilVideoWriterBackend final : public AlgoBackend {
    std::vector<Metavision::EventCD> passthrough_;
public:
    UtilVideoWriterBackend(int, int) {}
    void set_param(const std::string&, const std::string&) override {}
    std::string get_param(const std::string&) const override { return {}; }
    void push_events(const Metavision::EventCD* b, const Metavision::EventCD* e) override {
        passthrough_.assign(b, e);
    }
    AlgoResult pull_result() override {
        AlgoResult r;
        r.filtered_events = passthrough_;
        r.status = "video_writer: use Export menu";
        return r;
    }
    void reset() override { passthrough_.clear(); }
};


// --- Per-category factory (called by create_algo_backend in backend_factory.cpp)
std::unique_ptr<AlgoBackend> create_openeb_util_backend(const std::string& name,
                                          int width, int height) {
    if (name == "util_frame_composer")         return std::make_unique<UtilFrameComposerBackend>(width, height);
    if (name == "util_rolling_buffer")         return std::make_unique<UtilRollingBufferBackend>(width, height);
    if (name == "util_data_synchronizer")      return std::make_unique<UtilDataSynchronizerBackend>(width, height);
    if (name == "util_timing_profiler")        return std::make_unique<UtilTimingProfilerBackend>(width, height);
    if (name == "util_rate_estimator")         return std::make_unique<UtilRateEstimatorBackend>(width, height);
    if (name == "util_video_writer")           return std::make_unique<UtilVideoWriterBackend>(width, height);
    return nullptr;
}

} // namespace gui
