// gui/algo_bridge/algo_bridge.h — bridge between the Qt GUI layer and the
// algo/ C++ algorithm modules (self-developed) plus the OpenEB built-in
// algorithms (wrapped).
//
// Design reference: design.md §3.8 and §4.
//
// Phase 1 status: interface skeleton. The registry lists every algorithm
// (27 OpenEB capabilities + 19 self-developed modules) so the GUI can present
// them. create() returns a pass-through AlgoInstance stub; real per-algorithm
// wiring is added in Phases 5-9.

#ifndef GUI_ALGO_BRIDGE_ALGO_BRIDGE_H
#define GUI_ALGO_BRIDGE_ALGO_BRIDGE_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>

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

/// Result of one algorithm processing pull.
struct AlgoResult {
    std::vector<Metavision::EventCD> filtered_events; ///< pass-through output (for filters)
    std::string status;
    bool has_frame{false};
};

/// A live algorithm instance. Phase 1: pass-through stub.
///
/// Methods are thread-safe: push_events() is called from the SDK data thread
/// while set_param / set_enabled / pull_result are called from the GUI thread.
class AlgoInstance {
public:
    explicit AlgoInstance(const AlgoInfo& info);

    const AlgoInfo& info() const { return info_; }

    void set_param(const std::string& key, const std::string& value);
    std::string get_param(const std::string& key) const;

    void set_enabled(bool e);
    bool is_enabled() const;

    /// Push events to the algorithm. Thread-safe.
    void push_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

    /// Pull the latest result. Phase 1: returns the buffered events verbatim.
    AlgoResult pull_result();

private:
    AlgoInfo info_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> param_values_;
    std::vector<Metavision::EventCD> buffer_;
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

    /// @brief Looks up a live instance by name. Returns nullptr if no live
    /// instance exists (either never created or already destroyed).
    /// Used by ConfigManager to capture/apply runtime parameter values.
    std::shared_ptr<AlgoInstance> find_live(const std::string& name);

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
};

} // namespace gui

#endif // GUI_ALGO_BRIDGE_ALGO_BRIDGE_H
