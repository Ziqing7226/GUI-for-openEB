// gui/calibration/calibration_event_tap.h — 1 ms event accumulator for the
// calibration wizard.
//
// Subscribes to CameraController::cd_events_ready and buffers batches. The
// wizard calls drain_and_pick_max_window() on its 50 ms QTimer tick to extract
// the 1 ms sub-window with the most events — the one aligned with the
// chessboard's 20 Hz flip burst.
//
// The connection uses Qt::DirectConnection so on_events_ready() runs on the
// SDK streaming thread (where the signal is emitted), NOT the GUI thread.
// This is deliberate: a queued connection would post every batch to the GUI
// thread's event queue, and when findChessboardCorners blocks the GUI thread
// for 100+ ms, that queue grows without bound — each batch is a heap-allocated
// shared_ptr<vector<EventCD>> — and the process is OOM-killed. With a direct
// connection, batches go straight into buffer_ (under mutex_) and never enter
// the event queue. on_events_ready() is thread-safe (mutex only, no GUI) and
// the destructor disconnects + drains to prevent use-after-free.

#ifndef GUI_CALIBRATION_CALIBRATION_EVENT_TAP_H
#define GUI_CALIBRATION_CALIBRATION_EVENT_TAP_H

#include <cstddef>
#include <QObject>
#include <memory>
#include <mutex>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/utils/timestamp.h>

namespace gui {

class CameraController;

class CalibrationEventTap : public QObject {
    Q_OBJECT
public:
    explicit CalibrationEventTap(QObject* parent = nullptr);
    ~CalibrationEventTap();

    /// @brief Connects to @p camera's cd_events_ready signal. Safe to call
    /// with nullptr (drain becomes a no-op).
    void attach(CameraController* camera);

    /// @brief Drains the buffered events, slices the buffered prefix that
    ///        falls in [t_first, t_first + span_us) into window_us-wide
    ///        sub-windows, and copies the sub-window with the most events
    ///        into @p out. Returns that sub-window's event count (0 if the
    ///        buffer was empty or had fewer than one full sub-window).
    ///        The consumed prefix is dropped from the buffer; events beyond
    ///        t_first + span_us are retained for the next call.
    ///        Complexity is O(N) over the drained events — just a linear
    ///        scan with per-window counters, no image work.
    std::size_t drain_and_pick_max_window(Metavision::timestamp window_us,
                                          Metavision::timestamp span_us,
                                          std::vector<Metavision::EventCD>& out);

    /// @brief Drops all buffered events (e.g. when the user starts a fresh
    /// capture session).
    void clear();

private slots:
    void on_events_ready(std::shared_ptr<std::vector<Metavision::EventCD>> events);

private:
    std::mutex mutex_;
    std::vector<Metavision::EventCD> buffer_;
    CameraController* camera_{nullptr};

    /// Cap on buffer_.size(). When the GUI thread is blocked (e.g. by
    /// findChessboardCorners) and the SDK thread keeps appending, the buffer
    /// is trimmed to its tail (most recent events). 2 M events ≈ 32 MB for
    /// EventCD — enough for one 50 ms cycle at 40 M events/s, the worst case
    /// for a 1280×720 sensor looking at a flashing chessboard.
    static constexpr std::size_t kMaxBufferEvents = 2'000'000;
};

} // namespace gui

#endif // GUI_CALIBRATION_CALIBRATION_EVENT_TAP_H
