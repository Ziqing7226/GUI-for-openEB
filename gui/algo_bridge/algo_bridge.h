// gui/algo_bridge/algo_bridge.h — bridge between the Qt GUI layer and the
// algo/ C++ algorithm modules (self-developed) plus the OpenEB built-in
// algorithms (wrapped).
//
// Design reference: design.md §3.8 and §4.
//
// The bridge真正实例化并调用 algo/cv 与 algo/analytics 的真实算法类。
// AlgoInstance 持有一个 AlgoBackend，push_events 时零拷贝 reinterpret_cast
// EventCD→gui_algo::Event 后调用真实 process()/filter()，pull_result 返回
// 过滤事件 + 叠加层 + 帧。注册表列出全部 31 个自研模块 + 30 个 openEB 能力。

#ifndef GUI_ALGO_BRIDGE_ALGO_BRIDGE_H
#define GUI_ALGO_BRIDGE_ALGO_BRIDGE_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>

#include "algo_backend.h"  // AlgoBackend, AlgoResult, Overlay* structs

namespace gui {

/// How an algorithm's result is presented in the UI (design §5.6.1).
enum class AlgoDisplayMode {
    Overlay,    ///< Drawn on top of the main display frame.
    Replace,    ///< Replaces the main display frame.
    Standalone, ///< Shown in an independent child window.
    Passive,    ///< No visual output (e.g. filters, rate estimators).
};

/// Specification of a single algorithm parameter.
struct AlgoParamSpec {
    std::string key;
    std::string display_name;
    std::string type;                 // "int" | "float" | "bool" | "enum" | "string"
    std::string default_value;
    std::string min_value;
    std::string max_value;
    std::vector<std::string> enum_values;
};

/// Static description of an algorithm.
struct AlgoInfo {
    std::string name;            // unique id, e.g. "noise_filter"
    std::string display_name;    // human readable
    std::string category;        // cv | analytics | calibration | openeb_filter | openeb_frame | openeb_preproc | openeb_util
    std::string source;          // "self" | "openeb"
    AlgoDisplayMode display_mode{AlgoDisplayMode::Passive};
    std::vector<AlgoParamSpec> params;
};

/// A live algorithm instance. Holds a real AlgoBackend that wraps an algo/ class.
///
/// Methods are thread-safe: push_events() is called from the SDK data thread
/// while set_param / set_enabled / pull_result are called from the GUI thread.
class AlgoInstance {
public:
    explicit AlgoInstance(const AlgoInfo& info, int width = 1280, int height = 720);

    const AlgoInfo& info() const { return info_; }

    void set_param(const std::string& key, const std::string& value);
    std::string get_param(const std::string& key) const;

    void set_enabled(bool e);
    bool is_enabled() const;

    /// Push events to the algorithm backend. Thread-safe.
    void push_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

    /// Pull the latest result (filtered events + overlay + frame).
    AlgoResult pull_result();

    /// Reset the underlying backend.
    void reset();

private:
    AlgoInfo info_;
    int width_;
    int height_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> param_values_;
    std::unique_ptr<AlgoBackend> backend_;
    bool enabled_{true};
};

/// @brief Unified algorithm-call bridge (design §3.8).
class AlgoBridge {
public:
    AlgoBridge();

    /// @brief Lists every registered algorithm (OpenEB + self-developed).
    std::vector<AlgoInfo> list_algos() const;

    /// @brief Looks up an algorithm description by name.
    const AlgoInfo* find(const std::string& name) const;

    /// @brief Creates an algorithm instance by name.
    /// @return Shared pointer to the instance, or nullptr if unknown.
    std::shared_ptr<AlgoInstance> create(const std::string& name);

    /// @brief Sets the actual sensor dimensions so new instances are created
    /// with the correct width/height instead of the 1280x720 default.
    void set_sensor_dimensions(int width, int height);

    /// @brief Looks up a live instance by name. Returns nullptr if no live
    /// instance exists (either never created or already destroyed).
    /// Used by ConfigManager to capture/apply runtime parameter values.
    std::shared_ptr<AlgoInstance> find_live(const std::string& name);

    /// @brief Returns all live instances (for batch event push / result pull).
    std::vector<std::shared_ptr<AlgoInstance>> list_live();

    void push_events(const std::shared_ptr<AlgoInstance>& inst,
                     const Metavision::EventCD* begin,
                     const Metavision::EventCD* end);

    AlgoResult pull_result(const std::shared_ptr<AlgoInstance>& inst);

private:
    void register_openeb_filters();
    void register_openeb_frame_modes();
    void register_openeb_preprocessors();
    void register_openeb_utils();
    void register_self_cv();
    void register_self_analytics();
    void register_self_calibration();

    std::unordered_map<std::string, AlgoInfo> registry_;
    /// Weak references to live instances so ConfigManager can query/apply
    /// runtime parameter values without owning the instances. Expired
    /// entries are pruned lazily on each lookup.
    std::unordered_map<std::string, std::weak_ptr<AlgoInstance>> live_instances_;
    mutable std::mutex live_mutex_;
    int sensor_w_{1280};
    int sensor_h_{720};
};

} // namespace gui

#endif // GUI_ALGO_BRIDGE_ALGO_BRIDGE_H
