// algo/analytics/active_marker.h — Active marker tracking with sliding window.
//
// Design §4.4.1. Sliding-window event clustering with event-count color
// mapping (fewer events = blue, more events = red). Uses heatmap thresholding
// + connected-component analysis to locate markers, and optionally estimates
// per-cluster flicker frequency. Inspired by the Lighthouse sliding-window
// marker scheme. Header-only.

#ifndef GUI_ALGO_ANALYTICS_ACTIVE_MARKER_H
#define GUI_ALGO_ANALYTICS_ACTIVE_MARKER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"

namespace gui_algo {

/// @brief Annotation for a detected active-marker cluster.
struct ClusterAnnotation {
    float cx{0.0f};
    float cy{0.0f};
    int event_count{0};
    float frequency_hz{0.0f};   ///< Estimated event frequency (0 if disabled)
    cv::Vec3b color{0, 0, 0};
};

/// @brief Active marker tracker with sliding-window heatmap clustering.
class ActiveMarker {
public:
    enum class Colormap {
        Jet,
        Inferno,
        Turbo,
    };

    /// @brief Constructs the tracker.
    /// @param width,height Sensor dimensions.
    /// @param window_ms Sliding window length in ms, [1, 100].
    /// @param heatmap_threshold Min event count for a pixel to be "hot".
    /// @param min_cluster_area Min connected-component area in px.
    /// @param colormap Heatmap color mapping.
    /// @param enable_freq_detect Enable per-cluster frequency estimation.
    ActiveMarker(int width, int height,
                 float window_ms = 20.0f,
                 int heatmap_threshold = 50,
                 int min_cluster_area = 3,
                 Colormap colormap = Colormap::Jet,
                 bool enable_freq_detect = false)
        : width_(width), height_(height),
          window_us_(static_cast<Metavision::timestamp>(clamp_window(window_ms) * 1000.0f)),
          heatmap_threshold_(clamp_threshold(heatmap_threshold)),
          min_cluster_area_(clamp_area(min_cluster_area)),
          colormap_(colormap),
          enable_freq_detect_(enable_freq_detect),
          heatmap_(static_cast<std::size_t>(width) * height, 0) {}

    /// @brief Adds a batch of events to the sliding window.
    void process(const Event* events, std::size_t n) {
        if (events == nullptr || n == 0) return;
        for (std::size_t i = 0; i < n; ++i) {
            const Event& e = events[i];
            buffer_.push_back(e);
            if (e.x < width_ && e.y < height_) {
                const std::size_t idx =
                    static_cast<std::size_t>(e.y) * width_ + e.x;
                ++heatmap_[idx];
            }
            if (e.t > latest_t_) latest_t_ = e.t;
        }
        prune();
    }

    /// @brief Runs cluster detection on the current heatmap and returns the
    ///        cluster annotations (centroid, event count, color, frequency).
    std::vector<ClusterAnnotation> analyze() {
        std::vector<ClusterAnnotation> out;
        if (width_ <= 0 || height_ <= 0) return out;
        // Threshold the heatmap into a binary mask.
        cv::Mat mask(height_, width_, CV_8UC1, cv::Scalar(0));
        // Determine max count for color normalization (merged with mask build).
        int max_count = heatmap_threshold_;
        for (int y = 0; y < height_; ++y) {
            auto* row = mask.ptr<std::uint8_t>(y);
            const std::size_t row_offset = static_cast<std::size_t>(y) * width_;
            for (int x = 0; x < width_; ++x) {
                const int cnt = heatmap_[row_offset + x];
                if (cnt >= heatmap_threshold_) row[x] = 255;
                if (cnt > max_count) max_count = cnt;
            }
        }
        cv::Mat labels, stats, centroids;
        const int n_labels =
            cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);
        for (int i = 1; i < n_labels; ++i) {
            const int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < min_cluster_area_) continue;
            ClusterAnnotation c;
            const int u = static_cast<int>(centroids.at<double>(i, 0));
            const int v = static_cast<int>(centroids.at<double>(i, 1));
            c.cx = static_cast<float>(centroids.at<double>(i, 0));
            c.cy = static_cast<float>(centroids.at<double>(i, 1));
            c.event_count = sum_region_count(u, v);
            const double norm = max_count > heatmap_threshold_
                                    ? static_cast<double>(c.event_count - heatmap_threshold_) /
                                          static_cast<double>(max_count - heatmap_threshold_)
                                    : 0.0;
            c.color = map_color(norm);
            if (enable_freq_detect_) {
                c.frequency_hz = estimate_frequency(u, v);
            }
            out.push_back(c);
        }
        return out;
    }

    /// @brief Renders the heatmap as a CV_8UC3 overlay (black = below threshold).
    cv::Mat render_overlay() const {
        cv::Mat img(height_, width_, CV_8UC3, cv::Scalar(0, 0, 0));
        if (width_ <= 0 || height_ <= 0) return img;
        int max_count = heatmap_threshold_;
        for (const auto c : heatmap_) {
            if (c > max_count) max_count = c;
        }
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * width_ + x;
                const int cnt = heatmap_[idx];
                if (cnt < heatmap_threshold_) continue;
                const double norm = max_count > heatmap_threshold_
                                        ? static_cast<double>(cnt - heatmap_threshold_) /
                                              static_cast<double>(max_count - heatmap_threshold_)
                                        : 0.0;
                img.at<cv::Vec3b>(y, x) = map_color(norm);
            }
        }
        return img;
    }

    void set_window_ms(float ms) {
        window_us_ = static_cast<Metavision::timestamp>(clamp_window(ms) * 1000.0f);
    }
    float window_ms() const {
        return static_cast<float>(window_us_) / 1000.0f;
    }

    void set_heatmap_threshold(int t) { heatmap_threshold_ = clamp_threshold(t); }
    int heatmap_threshold() const { return heatmap_threshold_; }

    void set_min_cluster_area(int a) { min_cluster_area_ = clamp_area(a); }
    int min_cluster_area() const { return min_cluster_area_; }

    void set_colormap(Colormap c) { colormap_ = c; }
    Colormap colormap() const { return colormap_; }

    void set_enable_freq_detect(bool e) { enable_freq_detect_ = e; }
    bool enable_freq_detect() const { return enable_freq_detect_; }

    /// @brief Clears the sliding window and heatmap.
    void reset() {
        buffer_.clear();
        std::fill(heatmap_.begin(), heatmap_.end(), 0);
        latest_t_ = 0;
    }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    static float clamp_window(float ms) {
        if (ms < 1.0f) return 1.0f;
        if (ms > 100.0f) return 100.0f;
        return ms;
    }
    static int clamp_threshold(int t) {
        if (t < 1) return 1;
        if (t > 1000) return 1000;
        return t;
    }
    static int clamp_area(int a) {
        if (a < 1) return 1;
        if (a > 10000) return 10000;
        return a;
    }

    cv::Vec3b map_color(double v) const {
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        const auto b8 = [](double x) {
            return static_cast<std::uint8_t>(x * 255.0 + 0.5);
        };
        switch (colormap_) {
            case Colormap::Jet: {
                // blue -> cyan -> green -> yellow -> red
                double r = 0.0, g = 0.0, b = 0.0;
                if (v < 0.25) {
                    b = 1.0; g = v / 0.25;
                } else if (v < 0.5) {
                    b = 1.0 - (v - 0.25) / 0.25; g = 1.0;
                } else if (v < 0.75) {
                    g = 1.0; r = (v - 0.5) / 0.25;
                } else {
                    r = 1.0; g = 1.0 - (v - 0.75) / 0.25;
                }
                return cv::Vec3b(b8(b), b8(g), b8(r));
            }
            case Colormap::Inferno: {
                // black -> purple -> red -> orange -> yellow
                double r = v * 2.0;
                double g = std::max(0.0, (v - 0.5) * 2.0);
                double b = std::max(0.0, 0.4 - 0.8 * v);
                if (r > 1.0) r = 1.0;
                return cv::Vec3b(b8(b), b8(g), b8(r));
            }
            case Colormap::Turbo: {
                // Turbo perceptually-uniform colormap (Mikhailov 2019).
                // Built once via cv::applyColorMap(COLORMAP_TURBO) and cached
                // as a 256-entry LUT, so per-pixel lookup is O(1).
                static const cv::Mat lut = []() {
                    cv::Mat linear(1, 256, CV_8UC1);
                    for (int i = 0; i < 256; ++i) linear.at<uchar>(0, i) = i;
                    cv::Mat out;
                    cv::applyColorMap(linear, out, cv::COLORMAP_TURBO);
                    return out;
                }();
                const int idx = static_cast<int>(v * 255.0 + 0.5);
                return lut.at<cv::Vec3b>(0, std::min(255, std::max(0, idx)));
            }
        }
        return cv::Vec3b(0, 0, 0);
    }

    int sum_region_count(int u, int v) const {
        int total = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = u + dx;
                const int y = v + dy;
                if (x < 0 || y < 0 || x >= width_ || y >= height_) continue;
                total += heatmap_[static_cast<std::size_t>(y) * width_ + x];
            }
        }
        return total;
    }

    /// @brief Estimates the event frequency at a cluster centroid via median
    ///        ISI of events in the 3x3 region. Returns 0 if insufficient data.
    float estimate_frequency(int u, int v) const {
        std::vector<Metavision::timestamp> ts;
        ts.reserve(64);
        for (const Event& e : buffer_) {
            if (std::abs(static_cast<int>(e.x) - u) <= 1 &&
                std::abs(static_cast<int>(e.y) - v) <= 1) {
                ts.push_back(e.t);
            }
        }
        if (ts.size() < 3) return 0.0f;
        std::sort(ts.begin(), ts.end());
        std::vector<double> isis;
        isis.reserve(ts.size() - 1);
        for (std::size_t i = 1; i < ts.size(); ++i) {
            if (ts[i] > ts[i - 1]) {
                isis.push_back(static_cast<double>(ts[i] - ts[i - 1]));
            }
        }
        if (isis.empty()) return 0.0f;
        // Median ISI in us -> frequency in Hz.
        std::sort(isis.begin(), isis.end());
        const double median = isis[isis.size() / 2];
        if (median <= 0.0) return 0.0f;
        return static_cast<float>(1.0e6 / median);
    }

    void prune() {
        const Metavision::timestamp t_lo = latest_t_ - window_us_;
        while (!buffer_.empty() && buffer_.front().t < t_lo) {
            const Event& e = buffer_.front();
            if (e.x < width_ && e.y < height_) {
                const std::size_t idx =
                    static_cast<std::size_t>(e.y) * width_ + e.x;
                if (heatmap_[idx] > 0) --heatmap_[idx];
            }
            buffer_.pop_front();
        }
    }

    int width_;
    int height_;
    Metavision::timestamp window_us_;
    int heatmap_threshold_;
    int min_cluster_area_;
    Colormap colormap_;
    bool enable_freq_detect_;
    std::deque<Event> buffer_;
    std::vector<int> heatmap_;
    Metavision::timestamp latest_t_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_ANALYTICS_ACTIVE_MARKER_H
