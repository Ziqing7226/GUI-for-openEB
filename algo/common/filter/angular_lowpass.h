// algo/common/filter/angular_lowpass.h — angular low-pass filter (circular).
//
// Inspired by jAER AngleLowPassFilter. Low-pass filter for angular quantities
// that wrap around at 2π (e.g. motion direction, orientation). Uses vector
// averaging: accumulates (cos θ, sin θ) components and recovers the mean
// angle via atan2, which correctly handles the 0/2π boundary. Header-only.

#ifndef GUI_ALGO_COMMON_FILTER_ANGULAR_LOWPASS_H
#define GUI_ALGO_COMMON_FILTER_ANGULAR_LOWPASS_H

#include <cmath>

namespace gui_algo {

/// @brief Low-pass filter for angular quantities with 2π wrap-around.
class AngularLowpassFilter {
public:
    /// @brief Constructs the filter.
    /// @param alpha Smoothing factor in (0, 1]. 1 = no smoothing.
    explicit AngularLowpassFilter(double alpha = 0.2)
        : alpha_(alpha), sin_sum_(0.0), cos_sum_(0.0), init_(false) {}

    /// @brief Filters a new angle (radians) and returns the smoothed angle.
    double process(double theta) {
        const double c = std::cos(theta), s = std::sin(theta);
        if (!init_) {
            sin_sum_ = s;
            cos_sum_ = c;
            init_ = true;
        } else {
            sin_sum_ = alpha_ * s + (1.0 - alpha_) * sin_sum_;
            cos_sum_ = alpha_ * c + (1.0 - alpha_) * cos_sum_;
        }
        return std::atan2(sin_sum_, cos_sum_);
    }

    /// @brief Returns the current smoothed angle in [-π, π].
    double value() const {
        return init_ ? std::atan2(sin_sum_, cos_sum_) : 0.0;
    }

    /// @brief Returns the resultant length in [0, 1]; low values indicate the
    /// angle distribution is dispersed (unreliable mean).
    double coherence() const {
        if (!init_) return 0.0;
        return std::sqrt(sin_sum_ * sin_sum_ + cos_sum_ * cos_sum_);
    }

    bool initialized() const { return init_; }

    void set_alpha(double a) { alpha_ = a; }
    void reset() { sin_sum_ = 0.0; cos_sum_ = 0.0; init_ = false; }

private:
    double alpha_;
    double sin_sum_;
    double cos_sum_;
    bool init_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_FILTER_ANGULAR_LOWPASS_H
