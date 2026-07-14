// gui/app/statistics_controller.h — event-rate + ON/OFF statistics.
//
// Owns a Metavision::RateEstimator. The rate callback (fired on the SDK data
// thread) is marshalled to the GUI thread via a queued signal. ON/OFF counts
// are accumulated atomically in the CD callback and flushed to the GUI thread
// by a 10 Hz QTimer.

#ifndef GUI_APP_STATISTICS_CONTROLLER_H
#define GUI_APP_STATISTICS_CONTROLLER_H

#include <QObject>
#include <QTimer>
#include <atomic>
#include <cstdint>
#include <memory>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/utils/timestamp.h>
#include <metavision/sdk/core/utils/rate_estimator.h>

namespace gui {

class StatisticsController : public QObject {
    Q_OBJECT
public:
    explicit StatisticsController(QObject* parent = nullptr);
    ~StatisticsController();

    /// @brief Thread-safe: called from the SDK CD callback.
    void add_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);
    void reset();

signals:
    void rate_updated(double rate_eps, double peak_eps, Metavision::timestamp t);
    void on_off_updated(std::uint64_t on_count, std::uint64_t off_count, double on_ratio);

private:
    std::unique_ptr<Metavision::RateEstimator> rate_estimator_;
    std::atomic<std::uint64_t> on_count_{0};
    std::atomic<std::uint64_t> off_count_{0};
    QTimer on_off_timer_;
    /// Last emitted on/off counts — used to skip redundant signal emissions
    /// when no new events have arrived between timer ticks.
    std::uint64_t last_on_{0};
    std::uint64_t last_off_{0};
};

} // namespace gui

#endif // GUI_APP_STATISTICS_CONTROLLER_H
