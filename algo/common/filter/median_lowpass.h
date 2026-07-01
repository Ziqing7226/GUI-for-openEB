// algo/common/filter/median_lowpass.h — median low-pass filter (impulse denoiser).
//
// Inspired by jAER MedianLowPassFilter. A running median filter that removes
// impulsive outliers (spikes) from a scalar signal while preserving edges.
// Maintains a sliding window of recent samples and returns their median on each
// update. Window size is odd; even sizes are rounded up. Header-only.

#ifndef GUI_ALGO_COMMON_FILTER_MEDIAN_LOWPASS_H
#define GUI_ALGO_COMMON_FILTER_MEDIAN_LOWPASS_H

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>

namespace gui_algo {

/// @brief Running median filter for scalar signals (impulse noise removal).
class MedianLowpassFilter {
public:
    /// @brief Constructs the filter.
    /// @param window_size Number of samples in the sliding window (odd).
    explicit MedianLowpassFilter(std::size_t window_size = 5)
        : window_size_(window_size == 0 ? 1 : window_size) {}

    /// @brief Pushes a new sample and returns the median of the current window.
    double process(double x) {
        window_.push_back(x);
        if (window_.size() > window_size_) window_.pop_front();
        return median();
    }

    /// @brief Current median of the window (without pushing).
    double value() const { return median(); }

    std::size_t window_size() const { return window_size_; }
    void set_window_size(std::size_t n) {
        window_size_ = n == 0 ? 1 : n;
        while (window_.size() > window_size_) window_.pop_front();
    }

    void reset() { window_.clear(); }

private:
    double median() const {
        if (window_.empty()) return 0.0;
        std::vector<double> tmp(window_.begin(), window_.end());
        std::sort(tmp.begin(), tmp.end());
        const std::size_t n = tmp.size();
        return (n % 2 == 1) ? tmp[n / 2] : 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
    }

    std::size_t window_size_;
    std::deque<double> window_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_FILTER_MEDIAN_LOWPASS_H
