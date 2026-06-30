// gui/main_window.h — top-level QMainWindow.
//
// Layout (design §5.1):
//   - central:  EventDisplayWidget (OpenGL)
//   - right dock: SettingsPanel
//   - bottom (status bar): connection | event rate | timestamp | recording state
//   - menu bar: File | View | Camera | Preprocess | Frame Mode | Algorithm |
//               Calibration | Tools | Help
//
// Phase 1 wires: File (Open File / Exit), Camera (Connect / Disconnect / Device
// List / Refresh), View (panel toggles), Help (About). Other menus are stubs
// for later phases.

#ifndef GUI_MAIN_WINDOW_H
#define GUI_MAIN_WINDOW_H

#include <QMainWindow>
#include <memory>

#include "algo_bridge/algo_bridge.h"
#include "app/camera_controller.h"
#include "display/event_display_widget.h"
#include "panels/settings_panel.h"

class QLabel;
class QAction;

namespace gui {

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

private:
    void build_menus();
    void build_status_bar();
    void wire_signals();
    void update_palettes(int index);
    void forward_panel_message(const QString& msg, bool isError);

    EventDisplayWidget* display_{nullptr};
    SettingsPanel* settings_{nullptr};

    QLabel* status_conn_{nullptr};
    QLabel* status_rate_{nullptr};
    QLabel* status_ts_{nullptr};
    QLabel* status_rec_{nullptr};

    QAction* a_save_cfg_{nullptr};
    QAction* a_load_cfg_{nullptr};
    QAction* a_save_biases_{nullptr};
    QAction* a_load_biases_{nullptr};
    QAction* a_roi_drag_{nullptr};

    CameraController camera_;
    AlgoBridge algo_bridge_;
};

} // namespace gui

#endif // GUI_MAIN_WINDOW_H
