// algo/cv/xyt_visualizer.h — XYT 3D event point cloud data layer.
//
// Design §4.3.25. Provides the data slicing + color mapping for a 3D x-y-t
// event point cloud (X = pixel column, Y = pixel row, T = depth/time axis).
// Replaces the legacy 2D Temporal Plot. Inspired by jAER
// SpaceTimeRollingEventDisplayMethod. The actual GL/VBO rendering lives in
// gui/display/space_time_display.h; this class only produces the point buffer.
// Header-only.

#ifndef GUI_ALGO_CV_XYT_VISUALIZER_H
#define GUI_ALGO_CV_XYT_VISUALIZER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"

namespace gui_algo {

/// @brief A single colored point in the XYT point cloud.
struct XYTPoint {
    float x{0.0f};   ///< Pixel column (X axis)
    float y{0.0f};   ///< Pixel row (Y axis)
    float t{0.0f};   ///< Normalized time/depth in [0, 1] within the window
    float r{0.0f};   ///< Red channel [0, 1]
    float g{0.0f};   ///< Green channel [0, 1]
    float b{0.0f};   ///< Blue channel [0, 1]
};

/// @brief Builds a rolling XYT point cloud from events for VBO rendering.
class XYTVisualizer {
public:
    enum class ColorMode {
        Polarity,  ///< ON = red, OFF = green
        Age,       ///< Age gradient: blue (old) -> green -> red (new)
    };

    /// @brief Constructs the visualizer.
    /// @param time_window_ms Display window length in ms, [10, 10000].
    /// @param color_mode Polarity- or age-based coloring.
    /// @param point_size GL point size in [0.5, 10].
    /// @param auto_rotate Enable automatic scene rotation (rendering hint).
    /// @param depth_shade Enable depth-based shading (rendering hint).
    XYTVisualizer(float time_window_ms = 50.0f,
                  ColorMode color_mode = ColorMode::Polarity,
                  float point_size = 2.5f,
                  bool auto_rotate = false,
                  bool depth_shade = false)
        : time_window_ms_(clamp_window(time_window_ms)),
          color_mode_(color_mode),
          point_size_(clamp_point(point_size)),
          auto_rotate_(auto_rotate),
          depth_shade_(depth_shade) {}

    /// @brief Appends a batch of events to the rolling buffer.
    void process(const Event* events, std::size_t n) {
        if (events == nullptr || n == 0) return;
        for (std::size_t i = 0; i < n; ++i) {
            buffer_.push_back(events[i]);
        }
        latest_t_ = buffer_.back().t;
        prune();
    }

    /// @brief Slices the rolling buffer to the current time window and applies
    ///        color mapping, returning the point cloud for VBO rendering.
    std::vector<XYTPoint> render() const {
        std::vector<XYTPoint> pts;
        if (buffer_.empty()) return pts;
        const Metavision::timestamp window_us =
            static_cast<Metavision::timestamp>(time_window_ms_ * 1000.0f);
        const Metavision::timestamp t_hi = latest_t_;
        const Metavision::timestamp t_lo = t_hi - window_us;
        pts.reserve(buffer_.size());
        for (const Event& e : buffer_) {
            if (e.t < t_lo) continue;  // outside window
            XYTPoint p;
            p.x = static_cast<float>(e.x);
            p.y = static_cast<float>(e.y);
            const float tn =
                window_us > 0
                    ? static_cast<float>(e.t - t_lo) / static_cast<float>(window_us)
                    : 0.0f;
            p.t = tn < 0.0f ? 0.0f : (tn > 1.0f ? 1.0f : tn);
            colorize(e, tn, p);
            pts.push_back(p);
        }
        return pts;
    }

    void set_time_window_ms(float ms) { time_window_ms_ = clamp_window(ms); }
    float time_window_ms() const { return time_window_ms_; }

    void set_color_mode(ColorMode m) { color_mode_ = m; }
    ColorMode color_mode() const { return color_mode_; }

    void set_point_size(float s) { point_size_ = clamp_point(s); }
    float point_size() const { return point_size_; }

    void set_auto_rotate(bool r) { auto_rotate_ = r; }
    bool auto_rotate() const { return auto_rotate_; }

    void set_depth_shade(bool d) { depth_shade_ = d; }
    bool depth_shade() const { return depth_shade_; }

    /// @brief Clears the rolling buffer.
    void clear() { buffer_.clear(); }

private:
    static float clamp_window(float ms) {
        if (ms < 10.0f) return 10.0f;
        if (ms > 10000.0f) return 10000.0f;
        return ms;
    }
    static float clamp_point(float s) {
        if (s < 0.5f) return 0.5f;
        if (s > 10.0f) return 10.0f;
        return s;
    }

    void colorize(const Event& e, float tn, XYTPoint& p) const {
        switch (color_mode_) {
            case ColorMode::Polarity: {
                if (e.is_on()) {
                    p.r = 1.0f; p.g = 0.0f; p.b = 0.0f;  // ON = red
                } else {
                    p.r = 0.0f; p.g = 0.7f; p.b = 0.0f;  // OFF = green
                }
                break;
            }
            case ColorMode::Age: {
                // tn in [0,1]: 0 = oldest (blue), 0.5 = green, 1 = newest (red)
                if (tn < 0.5f) {
                    const float a = tn * 2.0f;  // 0..1
                    p.r = 0.0f;
                    p.g = a;
                    p.b = 1.0f - a;
                } else {
                    const float a = (tn - 0.5f) * 2.0f;  // 0..1
                    p.r = a;
                    p.g = 1.0f - a;
                    p.b = 0.0f;
                }
                break;
            }
        }
    }

    void prune() {
        const Metavision::timestamp window_us =
            static_cast<Metavision::timestamp>(time_window_ms_ * 1000.0f);
        const Metavision::timestamp t_lo = latest_t_ - window_us;
        while (!buffer_.empty() && buffer_.front().t < t_lo) {
            buffer_.pop_front();
        }
    }

    float time_window_ms_;
    ColorMode color_mode_;
    float point_size_;
    bool auto_rotate_;
    bool depth_shade_;
    std::deque<Event> buffer_;
    Metavision::timestamp latest_t_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_XYT_VISUALIZER_H
