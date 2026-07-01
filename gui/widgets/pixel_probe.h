// gui/widgets/pixel_probe.h — per-pixel event probe popup.
//
// Design §1.6.1 (jAER CellStatsProber). A popup QWidget that, for a
// user-selected pixel, shows the recent event sequence statistics: total
// event count, ON/OFF counts, mean inter-spike interval (ISI), an ISI
// histogram rendered as simple text bars, and a list of the most recent
// event timestamps/polarities.
//
// Events are fed via push_events(); the host connects a MouseAdaptor (or the
// display widget) clicked() signal to set_selected_pixel() so the user
// picks a pixel by clicking the main display.

#ifndef GUI_WIDGETS_PIXEL_PROBE_H
#define GUI_WIDGETS_PIXEL_PROBE_H

#include <QWidget>
#include <cstdint>
#include <deque>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/utils/timestamp.h>

class QLabel;

namespace gui {

class PixelProbe : public QWidget {
    Q_OBJECT
public:
    explicit PixelProbe(QWidget* parent = nullptr);
    ~PixelProbe();

    /// @brief Sets the sensor geometry (for clamping the selected pixel).
    void set_sensor_geometry(int width, int height);

    /// @brief Feeds a batch of events into the rolling buffer. Thread-safe
    /// when called via Qt::QueuedConnection from a non-GUI thread.
    void push_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

public slots:
    /// @brief Selects the pixel to inspect (sensor coordinates).
    void set_selected_pixel(int x, int y);
    /// @brief Clears the event buffer and stats.
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

signals:
    void closed();

private:
    struct ProbeEvent {
        std::uint16_t x{0};
        std::uint16_t y{0};
        std::int8_t p{0};
        Metavision::timestamp t{0};
    };

    void recompute();
    void prune_buffer();

    int sensor_w_{0};
    int sensor_h_{0};
    int sel_x_{-1};
    int sel_y_{-1};

    // Rolling buffer of all recent events (capped; older events dropped).
    std::deque<ProbeEvent> buffer_;
    static constexpr std::size_t kMaxBuffer = 1 << 20;  // 1M events

    // Computed stats for the selected pixel (updated in recompute()).
    std::vector<ProbeEvent> pixel_events_;  // selected-pixel events, ascending t
    long total_count_{0};
    long on_count_{0};
    long off_count_{0};
    double mean_isi_us_{0.0};
    double rate_keps_{0.0};  // events/s in keps (kilo-events/s)
    std::vector<long> isi_hist_;       // bin counts
    std::vector<double> isi_bin_edges_us_;
    static constexpr int kIsiBins = 16;
    static constexpr double kIsiMaxMs = 100.0;  // histogram covers [0, 100] ms

    QLabel* header_label_{nullptr};
};

} // namespace gui

#endif // GUI_WIDGETS_PIXEL_PROBE_H
