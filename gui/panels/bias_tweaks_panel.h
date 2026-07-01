// gui/panels/bias_tweaks_panel.h — advanced DVS bias "tweaks".
//
// Design §1.6.7 (jAER DVSTweaks). Exposes four high-level bias abstractions
// backed by physical-quantity estimates (Nozaki & Delbruck 2018 style):
//   threshold       — log-intensity contrast threshold (→ diff_on/diff_off)
//   bandwidth       — photoreceptor −3 dB bandwidth in Hz (→ pr)
//   maxFiringRate   — per-pixel max event rate in Hz (→ refr)
//   onOffBalance    — ON/OFF sensitivity balance (→ diff_on/diff_off ratio)
//
// Each abstraction drives a slider plus a live physical-quantity readout.
// Edits are applied immediately through CameraController's I_LL_Biases
// facility; sensors lacking the underlying bias names degrade gracefully.

#ifndef GUI_PANELS_BIAS_TWEAKS_PANEL_H
#define GUI_PANELS_BIAS_TWEAKS_PANEL_H

#include <QWidget>
#include <string>

class QSlider;
class QLabel;
class QPushButton;

namespace gui {

class CameraController;

class BiasTweaksPanel : public QWidget {
    Q_OBJECT
public:
    explicit BiasTweaksPanel(QWidget* parent = nullptr);

public slots:
    /// @brief Populates the panel from the connected camera's bias facility.
    void on_camera_connected(CameraController* controller);
    /// @brief Disables the panel.
    void on_camera_disconnected();

signals:
    void info_message(const QString& msg);
    void error_message(const QString& msg);

private slots:
    void on_threshold_changed(int v);
    void on_bandwidth_changed(int v);
    void on_max_rate_changed(int v);
    void on_balance_changed(int v);
    void on_reset();

private:
    struct Snapshot {
        bool has_diff_on{false};
        bool has_diff_off{false};
        bool has_pr{false};
        bool has_refr{false};
        int diff_on{0};
        int diff_off{0};
        int pr{0};
        int refr{0};
        std::string name_diff_on;
        std::string name_diff_off;
        std::string name_pr;
        std::string name_refr;
    };

    void set_bias(const std::string& name, int value);
    void refresh_readouts();
    void populate_from_camera();
    void enable_controls(bool on);

    // Slider value → underlying bias value helpers (and inverse).
    int threshold_to_diff(int slider_v) const;   // returns mid diff value
    int bandwidth_to_pr(int hz) const;
    int max_rate_to_refr(int hz) const;
    void apply_threshold_and_balance();

    QSlider* threshold_slider_{nullptr};
    QSlider* bandwidth_slider_{nullptr};
    QSlider* max_rate_slider_{nullptr};
    QSlider* balance_slider_{nullptr};

    QLabel* threshold_readout_{nullptr};
    QLabel* bandwidth_readout_{nullptr};
    QLabel* max_rate_readout_{nullptr};
    QLabel* balance_readout_{nullptr};
    QLabel* hint_label_{nullptr};
    QPushButton* reset_btn_{nullptr};

    CameraController* controller_{nullptr};
    Snapshot snap_;
    bool applying_{false};   ///< guards against recursive apply during populate
};

} // namespace gui

#endif // GUI_PANELS_BIAS_TWEAKS_PANEL_H
