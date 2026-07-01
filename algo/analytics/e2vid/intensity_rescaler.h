// algo/analytics/e2vid/intensity_rescaler.h — Intensity rescaling (tone mapping).
//
// Design §4.4.2 (E2VID postprocessing). Rescales reconstructed image
// intensities to [0, 255] using (robust) min/max normalization. Optionally
// computes Imin/Imax automatically (auto-HDR) with a sliding-window median
// filter to smooth temporal jitter. Ported from rpg_e2vid
// utils/inference_utils.py::IntensityRescaler. Header-only.

#ifndef GUI_ALGO_ANALYTICS_E2VID_INTENSITY_RESCALER_H
#define GUI_ALGO_ANALYTICS_E2VID_INTENSITY_RESCALER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace gui_algo {

/// @brief Rescales image intensities to [0, 255] with optional auto-HDR.
class IntensityRescaler {
public:
    /// @brief Constructs the rescaler.
    /// @param auto_hdr If true, Imin/Imax are computed automatically.
    /// @param median_filter_size Sliding window size for auto-HDR smoothing.
    /// @param imin Manual min intensity (used when auto_hdr is false).
    /// @param imax Manual max intensity (used when auto_hdr is false).
    IntensityRescaler(bool auto_hdr = false,
                      int median_filter_size = 10,
                      float imin = 0.0f,
                      float imax = 1.0f)
        : auto_hdr_(auto_hdr),
          median_filter_size_(clamp_size(median_filter_size)),
          imin_(imin), imax_(imax) {}

    /// @brief Rescales a CV_32FC1 image (values in [0,1]) to CV_8UC1 [0,255].
    cv::Mat operator()(const cv::Mat& img) {
        if (img.empty()) return cv::Mat();
        cv::Mat src;
        if (img.type() != CV_32FC1) {
            img.convertTo(src, CV_32FC1);
        } else {
            src = img;
        }

        if (auto_hdr_) {
            double dmin, dmax;
            cv::minMaxLoc(src, &dmin, &dmax);
            float imin = static_cast<float>(dmin);
            float imax = static_cast<float>(dmax);
            // Ensure range is at least 0.1.
            imin = std::max(0.0f, std::min(imin, 0.45f));
            imax = std::min(1.0f, std::max(imax, 0.55f));

            // Sliding-window median filter for temporal smoothing.
            // Matches rpg_e2vid: window stabilizes at (median_filter_size + 1)
            // elements (odd count for a well-defined median).
            if (static_cast<int>(bounds_.size()) > median_filter_size_) {
                bounds_.pop_front();
            }
            bounds_.push_back({imin, imax});
            imin_ = median_scalar(bounds_, true);
            imax_ = median_scalar(bounds_, false);
        }

        cv::Mat dst;
        const float range = imax_ - imin_;
        if (range < 1e-6f) {
            dst = cv::Mat::zeros(src.size(), CV_8UC1);
        } else {
            // img = 255 * (img - Imin) / (Imax - Imin), clamped to [0, 255].
            cv::Mat tmp = (src - imin_) * (255.0f / range);
            cv::threshold(tmp, tmp, 255.0, 255.0, cv::THRESH_TRUNC);
            cv::threshold(tmp, tmp, 0.0, 0.0, cv::THRESH_TOZERO);
            tmp.convertTo(dst, CV_8UC1);
        }
        return dst;
    }

    void set_auto_hdr(bool v) { auto_hdr_ = v; }
    bool auto_hdr() const { return auto_hdr_; }
    void set_imin(float v) { imin_ = v; }
    float imin() const { return imin_; }
    void set_imax(float v) { imax_ = v; }
    float imax() const { return imax_; }
    void set_median_filter_size(int v) {
        median_filter_size_ = clamp_size(v);
    }
    int median_filter_size() const { return median_filter_size_; }

    void reset() {
        bounds_.clear();
        imin_ = 0.0f;
        imax_ = 1.0f;
    }

private:
    static int clamp_size(int v) {
        if (v < 1) return 1;
        if (v > 100) return 100;
        return v;
    }

    static float median_scalar(
        const std::deque<std::pair<float, float>>& bounds, bool get_min) {
        std::vector<float> vals;
        vals.reserve(bounds.size());
        for (const auto& b : bounds) {
            vals.push_back(get_min ? b.first : b.second);
        }
        std::sort(vals.begin(), vals.end());
        const std::size_t n = vals.size();
        if (n == 0) return get_min ? 0.0f : 1.0f;
        // Match numpy.median: average the two middle elements for even n.
        if (n % 2 == 0) {
            return (vals[n / 2 - 1] + vals[n / 2]) * 0.5f;
        }
        return vals[n / 2];
    }

    bool auto_hdr_;
    int median_filter_size_;
    float imin_;
    float imax_;
    std::deque<std::pair<float, float>> bounds_;
};

} // namespace gui_algo

#endif // GUI_ALGO_ANALYTICS_E2VID_INTENSITY_RESCALER_H
