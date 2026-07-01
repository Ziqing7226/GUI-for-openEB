// algo/cv/optical_gyro.h — Electronic Image Stabilization (EIS).
//
// Implements design §4.3.23 (jAER OpticalFlowGyroTracker). Estimates global
// camera motion (translation + rotation) between accumulated event frames and
// compensates incoming event coordinates by the inverse of the smoothed,
// accumulated motion, achieving electronic stabilisation. Translation is
// estimated via cv::phaseCorrelate; rotation via the weighted principal-axis
// angle (PCA) of active pixels. Header-only.

#ifndef GUI_ALGO_CV_OPTICAL_GYRO_H
#define GUI_ALGO_CV_OPTICAL_GYRO_H

#include <cmath>
#include <cstdint>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief Estimated global camera motion (translation + rotation).
struct MotionEstimate {
    float dx{0.0f};       ///< Translation in x (pixels).
    float dy{0.0f};       ///< Translation in y (pixels).
    float dtheta{0.0f};   ///< Rotation (degrees).
};

/// @brief Electronic image stabiliser estimating and compensating camera motion.
class OpticalGyro {
public:
    OpticalGyro(int width, int height,
                float stabilization_strength = 1.0f,
                float smoothing_window_ms = 100.0f)
        : width_(width), height_(height),
          strength_(stabilization_strength),
          window_us_(static_cast<Metavision::timestamp>(
              static_cast<double>(smoothing_window_ms) * 1000.0)),
          ema_alpha_(50.0f / (50.0f + smoothing_window_ms)),
          accum_(cv::Mat::zeros(height, width, CV_32FC1)),
          prev_(cv::Mat::zeros(height, width, CV_32FC1)) {}

    /// @brief Compensates incoming events in-place by the accumulated motion and
    ///        accumulates them; re-estimates motion on each window expiry.
    void process(MutableEventPacket& packet) {
        const float cx = static_cast<float>(width_) * 0.5f;
        const float cy = static_cast<float>(height_) * 0.5f;
        const float ang = -total_.dtheta * strength_;
        const float ca = std::cos(ang);
        const float sa = std::sin(ang);
        const float tx = -total_.dx * strength_;
        const float ty = -total_.dy * strength_;
        const float xmax = static_cast<float>(width_ - 1);
        const float ymax = static_cast<float>(height_ - 1);
        for (Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            const float x = static_cast<float>(e.x);
            const float y = static_cast<float>(e.y);
            float rx = cx + (x - cx) * ca - (y - cy) * sa + tx;
            float ry = cy + (x - cx) * sa + (y - cy) * ca + ty;
            if (rx < 0.0f) rx = 0.0f;
            else if (rx > xmax) rx = xmax;
            if (ry < 0.0f) ry = 0.0f;
            else if (ry > ymax) ry = ymax;
            e.x = static_cast<std::uint16_t>(rx);
            e.y = static_cast<std::uint16_t>(ry);
            accum_.at<float>(static_cast<int>(e.y), static_cast<int>(e.x)) += 1.0f;
            if (window_start_ < 0) window_start_ = e.t;
            last_t_ = e.t;
        }
        if (window_start_ >= 0 && last_t_ - window_start_ >= window_us_) {
            const MotionEstimate m = estimate_motion();
            const float a = ema_alpha_;
            smoothed_.dx      = a * m.dx      + (1.0f - a) * smoothed_.dx;
            smoothed_.dy      = a * m.dy      + (1.0f - a) * smoothed_.dy;
            smoothed_.dtheta  = a * m.dtheta  + (1.0f - a) * smoothed_.dtheta;
            total_.dx     += smoothed_.dx;
            total_.dy     += smoothed_.dy;
            total_.dtheta += smoothed_.dtheta;
        }
    }

    // Motion accessors ------------------------------------------------------
    MotionEstimate smoothed_motion() const { return smoothed_; }
    MotionEstimate total_motion() const { return total_; }

    // Parameter accessors ---------------------------------------------------
    float stabilization_strength() const { return strength_; }
    float smoothing_window_ms() const {
        return static_cast<float>(window_us_) / 1000.0f;
    }
    void set_stabilization_strength(float v) { strength_ = v; }
    void set_smoothing_window_ms(float v) {
        window_us_ = static_cast<Metavision::timestamp>(
            static_cast<double>(v) * 1000.0);
        ema_alpha_ = 50.0f / (50.0f + v);
    }

    void reset() {
        accum_.setTo(0);
        prev_.setTo(0);
        has_prev_ = false;
        window_start_ = -1;
        last_t_ = -1;
        smoothed_ = MotionEstimate{};
        total_ = MotionEstimate{};
    }

private:
    MotionEstimate estimate_motion() {
        MotionEstimate m;
        const double prev_sum = cv::sum(prev_)[0];
        const double accum_sum = cv::sum(accum_)[0];
        if (has_prev_ && prev_sum > 0.0 && accum_sum > 0.0) {
            const cv::Point2d t = cv::phaseCorrelate(prev_, accum_);
            m.dx = static_cast<float>(t.x);
            m.dy = static_cast<float>(t.y);
            m.dtheta = principal_angle(accum_) - principal_angle(prev_);
            if (m.dtheta > 90.0f) m.dtheta -= 180.0f;
            else if (m.dtheta < -90.0f) m.dtheta += 180.0f;
        }
        prev_ = accum_.clone();
        has_prev_ = true;
        accum_.setTo(0);
        window_start_ = last_t_;
        return m;
    }

    /// @brief Weighted principal-axis angle (degrees) of active pixels.
    static float principal_angle(const cv::Mat& frame) {
        double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0, w = 0.0;
        for (int y = 0; y < frame.rows; ++y) {
            const float* row = frame.ptr<float>(y);
            for (int x = 0; x < frame.cols; ++x) {
                const double v = static_cast<double>(row[x]);
                if (v <= 0.0) continue;
                w += v;
                sx += v * x;
                sy += v * y;
                sxx += v * x * x;
                syy += v * y * y;
                sxy += v * x * y;
            }
        }
        if (w <= 0.0) return 0.0f;
        const double mx = sx / w;
        const double my = sy / w;
        const double cxx = sxx / w - mx * mx;
        const double cyy = syy / w - my * my;
        const double cxy = sxy / w - mx * my;
        return static_cast<float>(
            0.5 * std::atan2(2.0 * cxy, cxx - cyy) * 180.0 / CV_PI);
    }

    int width_;
    int height_;
    float strength_;
    Metavision::timestamp window_us_;
    float ema_alpha_;
    cv::Mat accum_;
    cv::Mat prev_;
    bool has_prev_{false};
    Metavision::timestamp window_start_{-1};
    Metavision::timestamp last_t_{-1};
    MotionEstimate smoothed_;
    MotionEstimate total_;
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_OPTICAL_GYRO_H
