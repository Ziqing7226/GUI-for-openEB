// gui/display/contrast_controller.h — display contrast auto-controller.
//
// Design §1.6.7 (jAER DavisVideoContrastController). A small PID-style
// controller that adjusts the display accumulation window so that the
// rendered frame contrast stays near a target. For event-based displays the
// "contrast" proxy is the event rate (Mev/s) feeding the renderer: too few
// events → dim/noisy frame, too many → saturated frame. The controller
// nudges accumulation_time_us to drive the measured event rate toward
// target_event_rate_mev.
//
// Header-only.

#ifndef GUI_DISPLAY_CONTRAST_CONTROLLER_H
#define GUI_DISPLAY_CONTRAST_CONTROLLER_H

#include <algorithm>
#include <cmath>

namespace gui {

/// @brief PID-style auto contrast (accumulation-time) controller.
class ContrastController {
public:
    /// @brief Constructs the controller.
    /// @param target_event_rate_mev Target event rate in Mev/s.
    /// @param kp,ki,kd PID gains (rate is in Mev/s; output is in microseconds).
    /// @param min_acc_us,max_acc_us Accumulation-time clamp range.
    ContrastController(double target_event_rate_mev = 10.0,
                       double kp = 2000.0, double ki = 500.0, double kd = 0.0,
                       double min_acc_us = 1000.0, double max_acc_us = 200000.0)
        : target_(target_event_rate_mev),
          kp_(kp), ki_(ki), kd_(kd),
          min_acc_(min_acc_us), max_acc_(max_acc_us),
          acc_us_(clamp((min_acc_us + max_acc_us) * 0.5)) {}

    /// @brief Enables/disables automatic control. When disabled, update() is
    /// a no-op and accumulation_time_us() holds its last value.
    void set_auto_enabled(bool e) { auto_enabled_ = e; }
    bool auto_enabled() const { return auto_enabled_; }

    void set_target_event_rate_mev(double mev) { target_ = (mev > 0.0) ? mev : 1.0; }
    double target_event_rate_mev() const { return target_; }

    void set_gains(double kp, double ki, double kd) {
        kp_ = kp; ki_ = ki; kd_ = kd;
    }

    void set_accumulation_range_us(double min_us, double max_us) {
        min_acc_ = (min_us > 0.0) ? min_us : 1.0;
        max_acc_ = (max_us > min_acc_) ? max_us : min_acc_ + 1.0;
        acc_us_ = clamp(acc_us_);
    }

    /// @brief Manually overrides the accumulation time (also disables auto).
    void set_accumulation_time_us(double us) {
        auto_enabled_ = false;
        acc_us_ = clamp(us);
        // Reset integrator so re-enabling auto starts from a clean state.
        integral_ = 0.0;
        prev_error_ = 0.0;
    }

    /// @brief Current accumulation time in microseconds.
    double accumulation_time_us() const { return acc_us_; }

    /// @brief Current accumulation time in milliseconds (for UI display).
    double accumulation_time_ms() const { return acc_us_ * 0.001; }

    /// @brief Feeds a measured event rate and returns the new accumulation
    /// time (μs). When auto is disabled the current value is returned unchanged.
    double update(double measured_rate_mev) {
        if (!auto_enabled_) return acc_us_;
        if (measured_rate_mev < 0.0) measured_rate_mev = 0.0;

        // Error: positive when measured > target (too many events → shrink
        // accumulation window), negative when too few (→ grow window).
        const double error = measured_rate_mev - target_;
        integral_ += error;
        // Anti-windup: clamp the integral term contribution.
        const double integral_limit = (max_acc_ - min_acc_) / std::max(ki_, 1e-9);
        if (integral_ > integral_limit) integral_ = integral_limit;
        if (integral_ < -integral_limit) integral_ = -integral_limit;
        const double derivative = error - prev_error_;
        prev_error_ = error;

        // Output: larger accumulation time when error<0 (need more events).
        const double delta = -(kp_ * error + ki_ * integral_ + kd_ * derivative);
        acc_us_ = clamp(acc_us_ + delta);
        return acc_us_;
    }

    /// @brief Resets the controller state (keeps target/gains/clamp).
    void reset() {
        integral_ = 0.0;
        prev_error_ = 0.0;
        acc_us_ = clamp((min_acc_ + max_acc_) * 0.5);
    }

private:
    double clamp(double us) const {
        if (us < min_acc_) return min_acc_;
        if (us > max_acc_) return max_acc_;
        return us;
    }

    double target_{10.0};
    double kp_{2000.0}, ki_{500.0}, kd_{0.0};
    double min_acc_{1000.0}, max_acc_{200000.0};
    double acc_us_{50000.0};
    double integral_{0.0};
    double prev_error_{0.0};
    bool auto_enabled_{true};
};

} // namespace gui

#endif // GUI_DISPLAY_CONTRAST_CONTROLLER_H
