// gui/app/statistics_controller.cpp

#include "statistics_controller.h"

namespace gui {

StatisticsController::StatisticsController(QObject* parent) : QObject(parent) {
    rate_estimator_ = std::make_unique<Metavision::RateEstimator>(
        [this](Metavision::timestamp t, double rate, double peak) {
            // Runs on the SDK data thread. Marshal to GUI thread.
            emit rate_updated(rate, peak, t);
        },
        100000,  // 100 ms step
        1000000, // 1 s window
        false);

    on_off_timer_.setInterval(100); // 10 Hz
    connect(&on_off_timer_, &QTimer::timeout, this, [this]() {
        const std::uint64_t on = on_count_.load(std::memory_order_relaxed);
        const std::uint64_t off = off_count_.load(std::memory_order_relaxed);
        // Skip the signal (and downstream label repaints) when no new events
        // have arrived since the last tick. The ratio is derived from on/off,
        // so unchanged counts imply an unchanged ratio.
        if (on == last_on_ && off == last_off_) {
            return;
        }
        last_on_ = on;
        last_off_ = off;
        const std::uint64_t total = on + off;
        const double ratio = total == 0 ? 0.0 : static_cast<double>(on) / total;
        emit on_off_updated(on, off, ratio);
    });
    on_off_timer_.start();
}

StatisticsController::~StatisticsController() {
    on_off_timer_.stop();
}

void StatisticsController::add_events(const Metavision::EventCD* begin,
                                      const Metavision::EventCD* end) {
    if (begin == end || !rate_estimator_) {
        return;
    }
    std::uint64_t on = 0;
    std::uint64_t off = 0;
    for (const Metavision::EventCD* it = begin; it != end; ++it) {
        if (it->p) {
            ++on;
        } else {
            ++off;
        }
    }
    on_count_.fetch_add(on, std::memory_order_relaxed);
    off_count_.fetch_add(off, std::memory_order_relaxed);
    rate_estimator_->add_data(std::prev(end)->t, static_cast<size_t>(end - begin));
}

void StatisticsController::reset() {
    on_count_.store(0, std::memory_order_relaxed);
    off_count_.store(0, std::memory_order_relaxed);
    if (rate_estimator_) {
        rate_estimator_->reset_data();
    }
}

} // namespace gui
