// gui/panels/algorithms_panel.h — algorithm selection & parameter UI (design §3.8).
//
// Lists every registered algorithm from AlgoBridge, grouped by category, with
// an enable checkbox and an expandable parameter editor per algorithm.
//
// Phases 6-8 self-developed CV/analytics algorithms are fully implemented in
// algo/cv and algo/analytics, and wired through AlgoBackend instances in
// algo_backend.cpp. Enabling an algorithm here activates its real processing
// pipeline (push_events / pull_result) and routes results to the display via
// FrameAnnotator (Overlay), frame replacement (Replace), or standalone
// windows (Standalone).

#ifndef GUI_PANELS_ALGORITHMS_PANEL_H
#define GUI_PANELS_ALGORITHMS_PANEL_H

#include <QWidget>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "algo_bridge/algo_bridge.h"  // for AlgoInfo + AlgoInstance

namespace gui {

class AlgoBridge;

class AlgorithmsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AlgorithmsPanel(AlgoBridge* bridge, QWidget* parent = nullptr);

signals:
    void info_message(const QString& msg);
    /// @brief Emitted when an algorithm's enable state changes.
    void algorithm_toggled(const QString& name, bool enabled);

private:
    void build_ui();
    /// Forwards a parameter edit to the live AlgoInstance (if one exists for
    /// the named algorithm). Creates the instance lazily via bridge_->create()
    /// so parameter changes before the checkbox is toggled are still recorded
    /// in the registry for ConfigManager save/load.
    void apply_param(const std::string& algo_name,
                     const std::string& param_key,
                     const std::string& value);

    AlgoBridge* bridge_;
    /// Owns a long-lived copy of the registry so pointers handed to lambdas
    /// remain valid for the panel's lifetime. AlgoBridge::list_algos()
    /// returns by value, so storing iterators/pointers into its result would
    /// dangle after the temporary is destroyed.
    std::vector<AlgoInfo> algos_;
    /// Live algorithm instances created when the user enables an algorithm.
    /// Stored so parameter edits can be forwarded to the instance via
    /// set_param(), and so ConfigManager can query runtime values via
    /// AlgoBridge::find_live().
    std::unordered_map<std::string, std::shared_ptr<AlgoInstance>> live_instances_;
};

} // namespace gui

#endif // GUI_PANELS_ALGORITHMS_PANEL_H
