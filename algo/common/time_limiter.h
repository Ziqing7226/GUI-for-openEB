// algo/common/time_limiter.h — per-frame time budget enforcer.
//
// Inspired by jAER TimeLimiter. Prevents a single processing frame from
// exceeding a wall-clock budget (e.g. to keep the GUI responsive). The caller
// polls should_stop() during iteration over a large event batch; once the
// budget is exhausted, processing yields and the remaining events are deferred
// to the next frame. Also reports the fraction of the budget consumed so the
// scheduler can adjust batch sizes.

#ifndef GUI_ALGO_COMMON_TIME_LIMITER_H
#define GUI_ALGO_COMMON_TIME_LIMITER_H

#include <chrono>

namespace gui_algo {

/// @brief Enforces a wall-clock time budget per processing frame.
class TimeLimiter {
public:
    using Clock = std::chrono::steady_clock;

    /// @brief Constructs the limiter.
    /// @param budget_us Per-frame budget in microseconds. 0 = no limit.
    explicit TimeLimiter(std::int64_t budget_us = 33000)
        : budget_us_(budget_us) {}

    /// @brief Starts a new budget window. Call at the beginning of each frame.
    void start() {
        start_ = Clock::now();
        stopped_ = false;
    }

    /// @brief Returns true once the budget has been exhausted.
    bool should_stop() {
        if (budget_us_ <= 0 || stopped_) return stopped_;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 Clock::now() - start_).count();
        if (elapsed >= budget_us_) {
            stopped_ = true;
        }
        return stopped_;
    }

    /// @brief Force-marks the budget as exhausted (e.g. downstream signalled).
    void stop() { stopped_ = true; }

    /// @brief Elapsed time since start() in microseconds.
    std::int64_t elapsed_us() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   Clock::now() - start_).count();
    }

    /// @brief Fraction of the budget consumed in [0, 1] (1+ if overrun).
    double budget_fraction() const {
        if (budget_us_ <= 0) return 0.0;
        return static_cast<double>(elapsed_us()) / budget_us_;
    }

    std::int64_t budget_us() const { return budget_us_; }
    void set_budget_us(std::int64_t us) { budget_us_ = us; }

private:
    std::int64_t budget_us_;
    Clock::time_point start_{};
    bool stopped_{false};
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_TIME_LIMITER_H
