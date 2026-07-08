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

#include <QCheckBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "algo_bridge/algo_bridge.h"  // for AlgoInfo + AlgoInstance

// Forward declarations of Qt widgets (defined in the global namespace).
class QLabel;
class QComboBox;

namespace gui {

class AlgoBridge;

class AlgorithmsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AlgorithmsPanel(AlgoBridge* bridge, QWidget* parent = nullptr);

    /// @brief Programmatically sets the enable-checkbox state for @p name
    /// without emitting toggled signals. Used by MainWindow to keep the
    /// panel in sync with the Algorithm menu and AlgoWindow.
    void set_algo_enabled(const std::string& name, bool on);

signals:
    void info_message(const QString& msg);
    /// @brief Emitted when an algorithm's enable state changes.
    void algorithm_toggled(const QString& name, bool enabled);
    /// @brief Emitted when an algorithm is enabled from the sidebar and
    /// needs an AlgoWindow opened (Standalone/Overlay algos need a display).
    void open_algo_window_requested(const std::string& name);

private:
    void build_ui();
    /// Forwards a parameter edit to the live AlgoInstance (if one exists for
    /// the named algorithm). Creates the instance lazily via bridge_->create()
    /// so parameter changes before the checkbox is toggled are still recorded
    /// in the registry for ConfigManager save/load.
    void apply_param(const std::string& algo_name,
                     const std::string& param_key,
                     const std::string& value);
    /// Applies the global Algorithm ROI (x/y/w/h + enabled) to every live
    /// algorithm instance. Called whenever the user edits the global ROI
    /// controls at the top of the panel.
    void apply_global_roi();
    /// Builds the global Algorithm ROI selector group at the top of the panel.
    void build_roi_selector(QVBoxLayout* parent_layout);
    /// Shows/hides mode-scoped parameter rows for @p algo_name based on the
    /// currently selected index of its "mode" enum combobox. Params whose
    /// AlgoParamSpec::mode_filter does not contain the current mode index are
    /// hidden (label + field); common params (empty mode_filter) stay visible.
    void refresh_mode_visibility(const std::string& algo_name);

    /// One label + field widget pair for a parameter row, plus the
    /// mode_filter that decides whether the row is visible for the current
    /// mode (empty = always visible).
    struct ParamRow {
        QLabel* label{nullptr};
        QWidget* field{nullptr};
        std::string mode_filter;
    };
    /// Per-algorithm UI state for mode-scoped parameter visibility.
    struct AlgoPanelState {
        QComboBox* mode_combo{nullptr};
        std::vector<ParamRow> rows;
    };

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
    /// Enable checkboxes keyed by algo name, for programmatic sync.
    std::unordered_map<std::string, QCheckBox*> checkboxes_;
    /// Per-algo parameter-row state, used to toggle mode-scoped params when
    /// the "mode" enum combobox changes (see refresh_mode_visibility).
    std::unordered_map<std::string, AlgoPanelState> algo_panel_state_;

    /// Global Algorithm ROI controls (design §5.6.6). All self-developed
    /// algorithms share this ROI; per-algorithm roi_* params are removed
    /// from the parameter editors and controlled exclusively here.
    QCheckBox* roi_enabled_cb_{nullptr};
    QSpinBox* roi_x_sp_{nullptr};
    QSpinBox* roi_y_sp_{nullptr};
    QSpinBox* roi_w_sp_{nullptr};
    QSpinBox* roi_h_sp_{nullptr};
};

} // namespace gui

#endif // GUI_PANELS_ALGORITHMS_PANEL_H
