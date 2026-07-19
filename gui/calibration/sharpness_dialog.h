// gui/calibration/sharpness_dialog.h — live sharpness meter with rolling chart.
//
// Polls EventDisplayWidget::current_frame() at ~10 Hz and plots the
// variance-of-Laplacian (standard focus measure) as a rolling 2-second line
// chart. Higher = sharper. Useful for bias tuning and lens focus: point the
// camera at a high-contrast static scene and watch the line climb as focus
// improves.

#ifndef GUI_CALIBRATION_SHARPNESS_DIALOG_H
#define GUI_CALIBRATION_SHARPNESS_DIALOG_H

#include <QDialog>
#include <QWidget>

#include <vector>

class QLabel;
class QTimer;

namespace gui {

class EventDisplayWidget;

/// @brief Line chart that plots sharpness values over the last 2 seconds.
class SharpnessChart : public QWidget {
    Q_OBJECT
public:
    explicit SharpnessChart(QWidget* parent = nullptr);

    /// @brief Appends a sample tagged with the current wall-clock time.
    /// Samples older than 2 s are pruned. Pass a negative value to indicate
    /// "no data" (the chart shows a placeholder).
    void add_value(double value);

    /// @brief Clears all samples.
    void clear();

    /// @brief Sets a fixed Y-axis upper bound. The chart no longer auto-scales
    /// to the data range — the line stays at a stable scale so the user can
    /// compare readings across time. Pass 0 to revert to auto-scaling.
    void set_y_max(double max);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Sample {
        qint64 t_ms;
        double value;
    };
    std::vector<Sample> samples_;
    static constexpr qint64 kWindowMs = 2000;  ///< Rolling window width.
    double y_max_{0.0};  ///< Fixed Y-axis upper bound; 0 = auto-scale.
};

/// @brief Live sharpness meter dialog for the current event visualization frame.
class SharpnessDialog : public QDialog {
    Q_OBJECT
public:
    explicit SharpnessDialog(QWidget* parent = nullptr);
    ~SharpnessDialog();

    /// @brief Sets the display to poll. Safe to call with nullptr (the timer
    /// keeps running but the chart stays empty until a display is set).
    void set_display(EventDisplayWidget* display);

private slots:
    void on_tick();

private:
    EventDisplayWidget* display_{nullptr};
    QTimer* timer_{nullptr};
    SharpnessChart* chart_{nullptr};
    QLabel* hint_label_{nullptr};
};

} // namespace gui

#endif // GUI_CALIBRATION_SHARPNESS_DIALOG_H
