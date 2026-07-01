// algo/common/freme.h — FREME (Frequency Representation) event template.
//
// Inspired by jAER Freme / FremeExtractor. Stores a per-pixel frequency
// spectrum representation of the event stream: each pixel holds a small vector
// of frequency bins populated from inter-event intervals (ISI). Useful for
// flicker detection, vibration analysis, and frequency-selective filtering.
//
// This is a header-only template so the bin type (float/double/complex) can be
// chosen by the consumer.

#ifndef GUI_ALGO_COMMON_FREME_H
#define GUI_ALGO_COMMON_FREME_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "event.h"

namespace gui_algo {

/// @brief Per-pixel frequency representation (FREME) buffer.
///
/// @tparam BinT Numeric bin type (default float). Complex types are allowed
///         for full DFT-style spectra.
template <typename BinT = float>
class Freme {
public:
    /// @brief Constructs a FREME buffer.
    /// @param width,height Sensor dimensions.
    /// @param num_bins Number of frequency bins per pixel.
    /// @param sample_rate_hz Effective sampling rate for bin→Hz mapping.
    Freme(int width, int height, int num_bins, double sample_rate_hz = 1000.0)
        : width_(width), height_(height), num_bins_(num_bins),
          sample_rate_hz_(sample_rate_hz),
          last_ts_(static_cast<std::size_t>(width) * height_, -1),
          spectrum_(static_cast<std::size_t>(width) * height_,
                    std::vector<BinT>(num_bins, BinT{})) {}

    /// @brief Feeds an event: updates the per-pixel spectrum from the ISI
    /// since the previous event at the same pixel.
    void add_event(const Event& e) {
        if (e.x >= width_ || e.y >= height_) return;
        const std::size_t idx = static_cast<std::size_t>(e.y) * width_ + e.x;
        const auto prev = last_ts_[idx];
        last_ts_[idx] = e.t;
        if (prev < 0) return; // first event at this pixel: no ISI yet

        const auto dt_us = e.t - prev;
        if (dt_us <= 0) return;
        // Instantaneous frequency from ISI.
        const double freq_hz = 1.0e6 / static_cast<double>(dt_us);
        const int bin = frequency_to_bin(freq_hz);
        if (bin >= 0 && bin < num_bins_) {
            spectrum_[idx][bin] += BinT{1};
        }
    }

    /// @brief Returns the spectrum for pixel (x, y).
    const std::vector<BinT>& spectrum(int x, int y) const {
        return spectrum_[static_cast<std::size_t>(y) * width_ + x];
    }

    /// @brief Returns a flat HxWxB image suitable for visualisation (bin magnitude).
    /// Output type CV_32F with B channels.
    cv::Mat to_image() const {
        cv::Mat img(height_, width_, CV_32FC(num_bins_));
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const auto& spec = spectrum_[static_cast<std::size_t>(y) * width_ + x];
                float* px = img.ptr<float>(y, x);
                for (int b = 0; b < num_bins_; ++b) {
                    px[b] = static_cast<float>(spec[b]);
                }
            }
        }
        return img;
    }

    /// @brief Decays all bins by @p factor (exponential forgetting).
    void decay(BinT factor) {
        for (auto& spec : spectrum_) {
            for (auto& v : spec) v *= factor;
        }
    }

    void clear() {
        std::fill(last_ts_.begin(), last_ts_.end(),
                  static_cast<Metavision::timestamp>(-1));
        for (auto& spec : spectrum_) {
            std::fill(spec.begin(), spec.end(), BinT{});
        }
    }

    int frequency_to_bin(double freq_hz) const {
        // Nyquist ceiling = sample_rate_hz / 2; bins span [0, nyquist).
        const double nyquist = sample_rate_hz_ * 0.5;
        if (freq_hz < 0 || freq_hz >= nyquist) return -1;
        return static_cast<int>(freq_hz / nyquist * num_bins_);
    }

    double bin_to_frequency(int bin) const {
        const double nyquist = sample_rate_hz_ * 0.5;
        return static_cast<double>(bin) / num_bins_ * nyquist;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    int num_bins() const { return num_bins_; }
    double sample_rate_hz() const { return sample_rate_hz_; }

private:
    int width_;
    int height_;
    int num_bins_;
    double sample_rate_hz_;
    std::vector<Metavision::timestamp> last_ts_;
    std::vector<std::vector<BinT>> spectrum_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_FREME_H
