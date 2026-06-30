// gui/temporal/temporal_plot_window.h — x-t / y-t temporal cross-section plot.
//
// Phase 10 (design §3.2.3): a standalone top-level window that plots event
// coordinates along a selected pixel row (for X-T) or column (for Y-T)
// against time. Used for analysing event spatio-temporal distribution and
// sensor tuning.
//
// Parameters (design §3.2.3):
//   plot_axis            — X or Y direction (which coordinate to plot)
//   axis_position        — pixel coordinate of the cross-axis (the row for
//                          X-T, the column for Y-T); only events on that
//                          line are plotted
//   accumulation_time_ms — plot refresh interval (ms); events arriving
//                          between refreshes are accumulated silently
//   time_window_ms       — visible time span (ms); older samples are pruned
//   show_polarity        — colour ON/OFF events differently

#ifndef GUI_TEMPORAL_TEMPORAL_PLOT_WINDOW_H
#define GUI_TEMPORAL_TEMPORAL_PLOT_WINDOW_H

#include <QWidget>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/utils/timestamp.h>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace gui {

/// @brief Standalone temporal cross-section scatter-plot window (design §3.2.3).
class TemporalPlotWindow : public QWidget {
    Q_OBJECT
public:
    enum class Mode { XT, YT };

    explicit TemporalPlotWindow(QWidget* parent = nullptr);
    ~TemporalPlotWindow();

    /// @brief Sets the sensor geometry so axis_position can be clamped and
    /// the Y-axis range can be drawn correctly.
    void set_sensor_geometry(int width, int height);

    /// @brief Feeds a batch of events. Thread-safe via Qt::QueuedConnection
    /// if called from a non-GUI thread (use invokeMethod).
    void push_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

public slots:
    void set_mode(int mode);   ///< 0 = XT, 1 = YT
    void set_time_window_ms(int ms);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_mode_changed(int mode);
    void on_position_changed(int pos);

private:
    struct Sample {
        Metavision::timestamp t;
        std::int16_t coord;   ///< x (XT) or y (YT)
        std::int8_t  pol;     ///< polarity for colouring
    };

    Mode mode_{Mode::XT};
    int axis_position_{0};
    int accumulation_ms_{100};
    int window_ms_{1000};
    bool show_polarity_{true};

    int sensor_w_{0};
    int sensor_h_{0};

    std::vector<Sample> samples_;
    Metavision::timestamp t_min_{0};
    Metavision::timestamp t_max_{0};
    Metavision::timestamp last_repaint_ts_{0};

    QComboBox*  mode_combo_{nullptr};
    QSpinBox*   position_spin_{nullptr};
    QSpinBox*   accumulation_spin_{nullptr};
    QSpinBox*   window_spin_{nullptr};
    QCheckBox*  polarity_check_{nullptr};
    QLabel*     status_label_{nullptr};
    QPushButton* clear_btn_{nullptr};

    void update_position_range();
    void prune_old_samples();
};

} // namespace gui

#endif // GUI_TEMPORAL_TEMPORAL_PLOT_WINDOW_H
