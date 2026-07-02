// gui/main_window.h — top-level QMainWindow.
//
// Layout (design §5.1):
//   - central:  EventDisplayWidget (OpenGL)
//   - right dock: SettingsPanel
//   - bottom (status bar): connection | event rate | timestamp | recording state
//   - bottom (above status bar): PlaybackControls (file playback only)
//   - menu bar: File | View | Camera | Preprocess | Frame Mode | Algorithm |
//               Calibration | Tools | Help
//
// Phase 1-2: live camera + bias/roi/esp/trigger control.
// Phase 3:   recorder + playback controls.
// Phase 4:   exporter dialog + config manager.
// Phase 5:   preprocessing panel, algorithms panel, file tools panel.
// Phase 9:   calibration wizard.
// Phase 10:  temporal plot, multi-window layout, i18n, layout save/restore.

#ifndef GUI_MAIN_WINDOW_H
#define GUI_MAIN_WINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include <metavision/sdk/base/utils/callback_id.h>

#include "algo_bridge/algo_bridge.h"
#include "app/camera_controller.h"
#include "app/file_converter.h"
#include "config/config_manager.h"
#include "config/layout_manager.h"
#include "display/event_display_widget.h"
#include "display/frame_annotator.h"
#include "exporter/exporter_controller.h"
#include "panels/settings_panel.h"
#include "recorder/playback_controller.h"
#include "recorder/recorder_controller.h"
#include "temporal/temporal_plot_window.h"
#include "display/space_time_display.h"
#include "widgets/algo_window.h"
#include "widgets/multi_window_manager.h"

class QLabel;
class QAction;
class QMenu;

namespace gui {

class PlaybackControls;
class ExportDialog;
class CalibrationWizard;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void on_open_file();
    void on_connect_first();
    void on_disconnect();
    void on_refresh_devices();
    void on_about();
    void on_save_config();
    void on_load_config();
    void on_save_biases();
    void on_load_biases();
    void on_toggle_roi_drag(bool on);

    // Phase 3 — recording / playback.
    void on_record_start();
    void on_record_stop();
    void on_record_elapsed(std::chrono::seconds s);

    // Phase 4 — export / presets.
    void on_export_dialog();
    void on_apply_preset(int index);

    // Phase 9 — calibration.
    void on_intrinsic_wizard();

    // Phase 10 — temporal plot / multi-window / layout.
    void on_open_temporal_plot();
    void on_open_time_surface();
    void on_open_xyt_view();
    void on_open_event_to_video();
    void on_open_freq_detector();
    void on_open_algo_window(const std::string& algo_name);
    void on_add_display_window();
    void on_tile_windows();
    void on_save_layout();
    void on_load_layout();
    void on_reset_layout();

private:
    void build_menus();
    void build_status_bar();
    void wire_signals();
    void update_palettes(int index);
    void forward_panel_message(const QString& msg, bool isError);

    void on_file_opened_for_playback(const QString& path);

    // Phase 10 — temporal plot callback management. The CD callback is
    // installed on the live camera when the plot window opens and removed
    // when it closes (or when the camera disconnects). Using QPointer +
    // a stored CallbackId avoids the dangling-pointer race that existed
    // when the raw pointer was read from the SDK thread.
    void install_temporal_callback();
    void remove_temporal_callback();

    // Algorithm event/result pipeline — pushes CD events to all live
    // AlgoInstances and pulls results for overlay/replace/standalone display.
    void install_algo_callback();
    void remove_algo_callback();
    void process_algo_results(QImage& frame);

    EventDisplayWidget* display_{nullptr};
    SettingsPanel* settings_{nullptr};
    PlaybackControls* playback_controls_{nullptr};

    QLabel* status_conn_{nullptr};
    QLabel* status_rate_{nullptr};
    QLabel* status_ts_{nullptr};
    QLabel* status_rec_{nullptr};

    // File menu actions.
    QAction* a_save_cfg_{nullptr};
    QAction* a_load_cfg_{nullptr};
    QAction* a_save_biases_{nullptr};
    QAction* a_load_biases_{nullptr};
    QAction* a_export_{nullptr};
    QAction* a_record_start_{nullptr};
    QAction* a_record_stop_{nullptr};

    // Camera menu actions.
    QAction* a_roi_drag_{nullptr};

    // Preprocess menu actions.
    QMenu* m_preprocess_{nullptr};

    // Calibration menu actions.
    QMenu* m_calibration_{nullptr};

    // Tools menu.
    QMenu* m_tools_{nullptr};

    CameraController camera_;
    AlgoBridge algo_bridge_;

    // Phase 3.
    RecorderController recorder_;
    PlaybackController playback_;

    // Phase 4.
    ExporterController exporter_;
    ConfigManager config_;
    ExportDialog* export_dialog_{nullptr};

    // Phase 5.
    FileConverter file_converter_;

    // Phase 9 — owned lazily; built when the wizard is first opened.
    CalibrationWizard* calibration_wizard_{nullptr};

    // Phase 10 — temporal plot is lazily created; layout manager is owned
    // from construction; multi-window manager is owned lazily.
    QPointer<TemporalPlotWindow> temporal_plot_;
    std::optional<Metavision::CallbackId> temporal_cd_cb_id_;
    std::atomic<Metavision::timestamp> temporal_last_post_us_{0};
    std::unique_ptr<MultiWindowManager> multi_window_;
    std::unique_ptr<LayoutManager> layout_manager_;

    // Standalone algorithm windows (design §5.6.1 / §5.6.6).
    // xyt_visualizer keeps a dedicated SpaceTimeDisplay (QOpenGLWidget with
    // 3D rendering); all other algorithms use the generic AlgoWindow
    // (algo_windows_) for both parameter control and result display.
    QPointer<SpaceTimeDisplay> xyt_display_;
    std::shared_ptr<AlgoInstance> xyt_algo_;

    /// Generic AlgoWindow instances keyed by algo name (design §5.6.6).
    /// Every self-developed algorithm gets an AlgoWindow when enabled, so the
    /// user can adjust any parameter (including the 5 ROI params) at runtime.
    QHash<std::string, QPointer<AlgoWindow>> algo_windows_;

    /// Algorithm ROI actions keyed by algo name (the "算法ROI" checkable
    /// submenu item that toggles roi_enabled). Kept separate from
    /// algo_actions_ (the "Enable" item) so the two can be toggled
    /// independently.
    QHash<std::string, QAction*> algo_roi_actions_;

    // Algorithm event/result pipeline.
    std::optional<Metavision::CallbackId> algo_cd_cb_id_;
    std::atomic<Metavision::timestamp> algo_last_xyt_post_us_{0};
    FrameAnnotator annotator_;
    Metavision::timestamp prev_frame_ts_{0};

    /// Algorithm menu actions keyed by algo name (for checkable state sync
    /// between the menu, AlgorithmsPanel, and standalone windows).
    QHash<std::string, QAction*> algo_actions_;

    /// Draws the ROI rectangle of any enabled self-developed algorithm
    /// (design §5.6.6: all self-developed algos support ROI) on the main
    /// display frame so the user can see which region is being processed.
    /// Called from process_algo_results().
    void draw_roi_overlays(QImage& frame);
};

} // namespace gui

#endif // GUI_MAIN_WINDOW_H
