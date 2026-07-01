// algo/common/event_rate_estimator.h — IIR-smoothed event rate estimator.
//
// Inspired by jAER EventRateEstimator. Estimates instantaneous event rate
// (events/second, also reported in Mev/s) from the event stream using a
// first-order IIR low-pass filter for smoothing. Two modes are supported:
//   - Count-based: rate = N_events / dt over the last window
//   - IIR-smoothed: rate = alpha * instantaneous + (1 - alpha) * previous
// The estimator is fed event batches and queried for the current rate.

#ifndef GUI_ALGO_COMMON_EVENT_RATE_ESTIMATOR_H
#define GUI_ALGO_COMMON_EVENT_RATE_ESTIMATOR_H

#include <cstddef>

#include <metavision/sdk/base/utils/timestamp.h>

namespace gui_algo {

/// @brief IIR-smoothed event rate estimator (events/second).
class EventRateEstimator {
public:
    /// @brief Constructs the estimator.
    /// @param alpha_smoothing IIR smoothing factor in (0, 1]. 1.0 = no smoothing
    ///        (pure instantaneous), smaller = more smoothing. Corresponds to a
    ///        time constant tau ~ dt / alpha.
    /// @param window_us Optional accumulation window for count-based estimation.
    explicit EventRateEstimator(float alpha_smoothing = 0.2f,
                                Metavision::timestamp window_us = 10000)
        : alpha_(alpha_smoothing), window_us_(window_us) {}

    /// @brief Notifies the estimator of a batch of events.
    /// @param n Number of events in the batch.
    /// @param t Timestamp of the last event in the batch (us).
    void add_events(std::size_t n, Metavision::timestamp t) {
        if (n == 0) return;
        if (last_t_ < 0) {
            // First batch: seed counters, no rate yet.
            window_count_ += n;
            last_t_ = t;
            return;
        }
        window_count_ += n;
        const auto dt = t - last_t_;
        if (dt <= 0) return;
        // Instantaneous rate over this batch interval.
        const double instant_rate =
            static_cast<double>(n) / (static_cast<double>(dt) * 1.0e-6);
        if (rate_ < 0.0) {
            rate_ = instant_rate;
        } else {
            rate_ = alpha_ * instant_rate + (1.0f - alpha_) * rate_;
        }
        last_t_ = t;
    }

    /// @brief Returns the smoothed event rate in events/second, or 0 if unknown.
    double rate_eps() const { return rate_ < 0.0 ? 0.0 : rate_; }

    /// @brief Returns the smoothed event rate in Mev/s (mega-events/second).
    double rate_meps() const { return rate_eps() * 1.0e-6; }

    /// @brief Resets the estimator state.
    void reset() {
        rate_ = -1.0;
        window_count_ = 0;
        last_t_ = -1;
    }

    float alpha() const { return alpha_; }
    void set_alpha(float a) { alpha_ = a; }

private:
    float alpha_;
    Metavision::timestamp window_us_;
    double rate_{-1.0};              // events/second, -1 = uninitialized
    std::size_t window_count_{0};
    Metavision::timestamp last_t_{-1};
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_EVENT_RATE_ESTIMATOR_H
