// gui/widgets/algo_window.h — generic algorithm parameter + display dock.
//
// Design §5.6.6: every self-developed algorithm exposes an AlgoWindow after
// being enabled. The window auto-generates a parameter panel from the
// algorithm's AlgoParamSpec list (including the 5 ROI parameters) so the user
// can tune any algorithm at runtime. For Standalone algorithms the window
// also hosts the result display widget (EventDisplayWidget for frame
// producers, QLabel for text producers); for Overlay/Replace/Passive
// algorithms the display area shows the algorithm's status string and the
// visual output remains on the main display frame.
//
// AlgoWindow is a QDockWidget so it docks into the MainWindow (default:
// left edge), can be dragged to any edge, floated, or tabbed with other
// algo windows. This keeps all algorithm outputs within the main window
// frame instead of spawning separate floating windows.
//
// The window is opened by MainWindow when the algorithm is enabled, and is
// closed automatically when the algorithm is disabled. Closing the window
// disables the algorithm and unchecks the sidebar checkbox.

#ifndef GUI_WIDGETS_ALGO_WINDOW_H
#define GUI_WIDGETS_ALGO_WINDOW_H

#include <QDockWidget>
#include <QLabel>
#include <QString>
#include <QWidget>
#include <memory>
#include <string>

#include "algo_bridge/algo_bridge.h"

class QComboBox;
class QScrollArea;
class QVBoxLayout;

namespace gui {

class EventDisplayWidget;

class AlgoWindow : public QDockWidget {
    Q_OBJECT
public:
    /// @brief Constructs an AlgoWindow for the named algorithm.
    /// @p bridge must outlive the window. The constructor finds (or creates)
    /// the live AlgoInstance and enables it.
    AlgoWindow(AlgoBridge* bridge, const std::string& algo_name,
               QWidget* parent = nullptr);

    /// @brief Returns the algorithm name this window manages.
    const std::string& algo_name() const { return algo_name_; }

    /// @brief Returns the live instance managed by this window (may be null
    /// if the algorithm was unknown).
    std::shared_ptr<AlgoInstance> instance() const { return instance_; }

    /// @brief Returns the status QLabel (the default display widget).
    /// @note Only valid when no custom display widget has been installed.
    QLabel* status_label() const { return status_label_; }

    /// @brief Returns the frame display widget if one was installed, else
    /// nullptr. Used by MainWindow to route Standalone frame results.
    EventDisplayWidget* frame_display() const;

    /// @brief Installs a custom display widget, replacing the default status
    /// label. Ownership transfers to the window's layout.
    void set_display_widget(QWidget* w);

    /// @brief Updates the status label text (if present). Called from
    /// MainWindow::process_algo_results for text-based Standalone algos.
    void set_status_text(const QString& text);

signals:
    /// @brief Emitted when the window is about to close (closeEvent).
    /// MainWindow connects this to disable the algo instance and uncheck
    /// the sidebar checkbox.
    void closing(const std::string& algo_name);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    /// Builds the auto-generated parameter panel (same widget pattern as
    /// AlgorithmsPanel). Parameters are sorted with the 5 ROI params at the
    /// top so the user can quickly toggle/resize the ROI region.
    void build_param_panel(QVBoxLayout* outer);

    /// Forwards a parameter edit to the live AlgoInstance.
    void apply_param(const std::string& key, const std::string& value);

    /// Shows/hides mode-scoped parameter rows based on the currently selected
    /// index of the "mode" enum combobox (mirrors AlgorithmsPanel). Params
    /// whose AlgoParamSpec::mode_filter does not contain the current mode
    /// index are hidden; common params (empty mode_filter) stay visible.
    void refresh_mode_visibility();

    /// One label + field widget pair for a parameter row, plus the
    /// mode_filter that decides whether the row is visible for the current
    /// mode (empty = always visible).
    struct ParamRow {
        QLabel* label{nullptr};
        QWidget* field{nullptr};
        std::string mode_filter;
        std::string key;  ///< Parameter key (e.g. "roi_w") — used to locate
                          ///< specific rows programmatically (e.g. auto-ROI
                          ///< when switching to E2VID mode).
    };

    AlgoBridge* bridge_;
    std::string algo_name_;
    AlgoInfo info_;
    std::shared_ptr<AlgoInstance> instance_;

    QWidget* content_{nullptr};  ///< Inner widget set via QDockWidget::setWidget.
    QScrollArea* param_scroll_{nullptr};
    QLabel* status_label_{nullptr};
    QWidget* display_widget_{nullptr};
    QVBoxLayout* display_layout_{nullptr};

    /// Mode-scoped parameter visibility state (only used by algos that expose
    /// a "mode" enum, e.g. event_to_video — design §4.4.2).
    QComboBox* mode_combo_{nullptr};
    std::vector<ParamRow> param_rows_;
};

} // namespace gui

#endif // GUI_WIDGETS_ALGO_WINDOW_H
