// algo/analytics/auto_bias_controller.h — Adaptive bias control via PID.
//
// Design §4.4.6. PID controller that adjusts camera biases (bias_diff /
// bias_on / bias_off) based on event-rate feedback to maintain a target event
// rate, inspired by jAER AutoExposureController (which uses APS histograms;
// this module uses event rate as the feedback signal). Output: bias adjustment
// commands with rate limiting and clamping. Header-only.

#ifndef GUI_ALGO_ANALYTICS_AUTO_BIAS_CONTROLLER_H
#define GUI_ALGO_ANALYTICS_AUTO_BIAS_CONTROLLER_H

#include <algorithm>
#include <cmath>

#include <metavision/sdk/base/utils/timestamp.h>

namespace gui_algo {

/// @brief A bias adjustment command emitted by the controller.
struct BiasCommand {
    float delta_diff{0.0f};  ///< Adjustment for bias_diff
    float delta_on{0.0f};    ///< Adjustment for bias_on
    float delta_off{0.0f};   ///< Adjustment for bias_off
};

/// @brief PID-based adaptive camera bias controller targeting an event rate.
class AutoBiasController {
public:
    /// @brief Constructs the controller.
    /// @param target_event_rate_mev Target event rate in Mev/s, [0.1, 50].
    /// @param kp,ki,kd PID gains.
    /// @param max_step Maximum per-update step magnitude, [1, 100].
    AutoBiasController(float target_event_rate_mev = 5.0f,
                       float kp = 0.5f, float ki = 0.01f, float kd = 0.0f,
                       float max_step = 10.0f)
        : target_(clamp_target(target_event_rate_mev)),
          kp_(kp), ki_(ki), kd_(kd),
          max_step_(clamp_step(max_step)) {}

    /// @brief Produces a bias adjustment command from the measured event rate.
    /// @param measured_mev Current smoothed event rate in Mev/s.
    /// @param dt_us Time elapsed since the last update in us (for integral/
    ///        derivative terms). If <= 0, only the proportional term is used.
    BiasCommand update(double measured_mev, Metavision::timestamp dt_us) {
        const double error =
            static_cast<double>(target_) - measured_mev;
        // Proportional term.
        double p = kp_ * error;
        // Integral term (accumulated error over time).
        double d_term = 0.0;
        if (dt_us > 0) {
            const double dt_s = static_cast<double>(dt_us) * 1.0e-6;
            integral_ += error * dt_s;
            // Anti-windup: clamp the integral contribution.
            const double i_max = static_cast<double>(max_step_);
            if (integral_ > i_max) integral_ = i_max;
            if (integral_ < -i_max) integral_ = -i_max;
            // Derivative term.
            if (last_error_init_) {
                d_term = kd_ * (error - last_error_) / dt_s;
            }
            last_error_ = error;
            last_error_init_ = true;
        }
        const double i = ki_ * integral_;
        double output = p + i + d_term;
        // Rate-limit the output to [-max_step, +max_step].
        if (output > max_step_) output = max_step_;
        if (output < -max_step_) output = -max_step_;
        // Higher event rate than target => reduce bias (negative delta);
        // lower event rate than target => increase bias (positive delta).
        // error = target - measured, so positive error => need more events =>
        // increase bias => positive delta. This matches `output` directly.
        BiasCommand cmd;
        // Drive bias_diff primarily; bias_on/bias_off receive a fraction.
        cmd.delta_diff = static_cast<float>(output);
        cmd.delta_on = static_cast<float>(output) * 0.5f;
        cmd.delta_off = static_cast<float>(output) * 0.5f;
        last_output_ = output;
        return cmd;
    }

    void set_target_event_rate_mev(float r) { target_ = clamp_target(r); }
    float target_event_rate_mev() const { return target_; }

    void set_gains(float kp, float ki, float kd) {
        kp_ = kp; ki_ = ki; kd_ = kd;
    }
    float kp() const { return kp_; }
    float ki() const { return ki_; }
    float kd() const { return kd_; }

    void set_max_step(float s) { max_step_ = clamp_step(s); }
    float max_step() const { return max_step_; }

    double last_output() const { return last_output_; }
    double integral() const { return integral_; }

    /// @brief Resets the controller state (integral + derivative memory).
    void reset() {
        integral_ = 0.0;
        last_error_ = 0.0;
        last_error_init_ = false;
        last_output_ = 0.0;
    }

private:
    static float clamp_target(float r) {
        if (r < 0.1f) return 0.1f;
        if (r > 50.0f) return 50.0f;
        return r;
    }
    static float clamp_step(float s) {
        if (s < 1.0f) return 1.0f;
        if (s > 100.0f) return 100.0f;
        return s;
    }

    float target_;
    float kp_;
    float ki_;
    float kd_;
    float max_step_;
    double integral_{0.0};
    double last_error_{0.0};
    bool last_error_init_{false};
    double last_output_{0.0};
};

} // namespace gui_algo

#endif // GUI_ALGO_ANALYTICS_AUTO_BIAS_CONTROLLER_H
