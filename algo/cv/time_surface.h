// algo/cv/time_surface.h — Time Surface window (most-recent-timestamp buffer).
//
// Design §4.3.27. Maintains a per-pixel MostRecentTimestampBuffer (HxW) updated
// by incoming events, and renders a time-decay encoded pseudo-color image via a
// selectable palette (Gray/Hot/Plasma/Turbo). Wraps the openEB
// TimeSurfaceProcessor concept; supports merged (1 channel) or split-polarity
// (2 channel) buffers. Inspired by jAER TimeSurface / leatherboard utilities.
// Header-only.

#ifndef GUI_ALGO_CV_TIME_SURFACE_H
#define GUI_ALGO_CV_TIME_SURFACE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"

namespace gui_algo {

/// @brief Time Surface renderer with MostRecentTimestampBuffer + decay LUT.
class TimeSurface {
public:
    enum class Channels {
        Merged = 1,   ///< 1 channel: both polarities share one buffer.
        Split = 2,    ///< 2 channels: separate ON/OFF buffers.
    };

    enum class Palette {
        Gray,
        Hot,
        Plasma,
        Turbo,
    };

    /// @brief Constructs the time surface.
    /// @param width,height Sensor dimensions.
    /// @param channels Merged or split polarity buffers.
    /// @param decay_time_us Exponential decay time constant in us.
    /// @param palette Pseudo-color palette.
    /// @param refresh_rate_hz Target render refresh rate in Hz.
    TimeSurface(int width, int height,
                Channels channels = Channels::Merged,
                Metavision::timestamp decay_time_us = 100000,
                Palette palette = Palette::Hot,
                int refresh_rate_hz = 30)
        : width_(width), height_(height), channels_(channels),
          decay_time_us_(clamp_decay(decay_time_us)),
          palette_(palette),
          refresh_rate_hz_(clamp_refresh(refresh_rate_hz)),
          // Sentinel -1 marks pixels that have never received an event.
          ts_buf_(static_cast<std::size_t>(width) * height, -1),
          ts_on_(static_cast<std::size_t>(width) * height, -1),
          ts_off_(static_cast<std::size_t>(width) * height, -1) {}

    /// @brief Updates the MostRecentTimestampBuffer with a batch of events.
    void process(const Event* events, std::size_t n) {
        if (events == nullptr || n == 0) return;
        for (std::size_t i = 0; i < n; ++i) {
            const Event& e = events[i];
            if (e.x >= width_ || e.y >= height_) continue;
            const std::size_t idx =
                static_cast<std::size_t>(e.y) * static_cast<std::size_t>(width_) + e.x;
            ts_buf_[idx] = e.t;
            if (channels_ == Channels::Split) {
                if (e.is_on()) {
                    ts_on_[idx] = e.t;
                } else {
                    ts_off_[idx] = e.t;
                }
            }
            if (e.t > current_t_) current_t_ = e.t;
        }
    }

    /// @brief Renders the time-decay encoded pseudo-color image (CV_8UC3).
    cv::Mat render() const {
        cv::Mat img(height_, width_, CV_8UC3,
                    cv::Scalar(0, 0, 0));
        if (width_ <= 0 || height_ <= 0) return img;
        if (channels_ == Channels::Merged) {
            for (int y = 0; y < height_; ++y) {
                auto* row = img.ptr<cv::Vec3b>(y);
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const Metavision::timestamp tp = ts_buf_[idx];
                    if (tp < 0) continue;
                    row[x] = map_color(decay_value(tp));
                }
            }
        } else {
            // Split polarity: ON uses full palette color, OFF uses dimmed
            // palette color so the palette setting actually takes effect.
            for (int y = 0; y < height_; ++y) {
                auto* row = img.ptr<cv::Vec3b>(y);
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const Metavision::timestamp tp_on = ts_on_[idx];
                    const Metavision::timestamp tp_off = ts_off_[idx];
                    if (tp_on < 0 && tp_off < 0) continue;
                    cv::Vec3b c(0, 0, 0);
                    if (tp_on >= 0) {
                        c = map_color(decay_value(tp_on));
                    }
                    if (tp_off >= 0) {
                        const cv::Vec3b c_off = map_color(decay_value(tp_off));
                        if (tp_on >= 0) {
                            c = (c + c_off) * 0.5;
                        } else {
                            c = cv::Vec3b(c_off[0] / 2, c_off[1] / 2, c_off[2] / 2);
                        }
                    }
                    row[x] = c;
                }
            }
        }
        return img;
    }

    /// @brief Returns the minimum render interval in us from refresh_rate_hz.
    Metavision::timestamp refresh_interval_us() const {
        return static_cast<Metavision::timestamp>(1.0e6 / refresh_rate_hz_);
    }

    void set_channels(Channels c) { channels_ = c; }
    Channels channels() const { return channels_; }

    void set_decay_time_us(Metavision::timestamp us) {
        decay_time_us_ = clamp_decay(us);
    }
    Metavision::timestamp decay_time_us() const { return decay_time_us_; }

    void set_palette(Palette p) { palette_ = p; }
    Palette palette() const { return palette_; }

    void set_refresh_rate_hz(int hz) { refresh_rate_hz_ = clamp_refresh(hz); }
    int refresh_rate_hz() const { return refresh_rate_hz_; }

    /// @brief Clears the timestamp buffers.
    void reset() {
        std::fill(ts_buf_.begin(), ts_buf_.end(),
                  static_cast<Metavision::timestamp>(-1));
        std::fill(ts_on_.begin(), ts_on_.end(),
                  static_cast<Metavision::timestamp>(-1));
        std::fill(ts_off_.begin(), ts_off_.end(),
                  static_cast<Metavision::timestamp>(-1));
        current_t_ = 0;
    }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    static Metavision::timestamp clamp_decay(Metavision::timestamp us) {
        if (us < 10000) return 10000;
        if (us > 5000000) return 5000000;
        return us;
    }
    static int clamp_refresh(int hz) {
        if (hz < 10) return 10;
        if (hz > 120) return 120;
        return hz;
    }

    /// @brief Exponential decay value in [0, 1] for a pixel last hit at @p tp.
    double decay_value(Metavision::timestamp tp) const {
        const Metavision::timestamp age = current_t_ - tp;
        if (age <= 0) return 1.0;
        const double a = static_cast<double>(age) /
                         static_cast<double>(decay_time_us_);
        const double v = std::exp(-a);
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }

    /// @brief Maps a normalized value in [0, 1] to a palette color.
    cv::Vec3b map_color(double v) const {
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        const auto b8 = [](double x) {
            return static_cast<std::uint8_t>(x * 255.0 + 0.5);
        };
        switch (palette_) {
            case Palette::Gray: {
                const std::uint8_t g = b8(v);
                return cv::Vec3b(g, g, g);
            }
            case Palette::Hot: {
                // black -> red -> yellow -> white
                double r = 0.0, g = 0.0, b = 0.0;
                if (v < 0.33) {
                    r = v / 0.33;
                } else if (v < 0.66) {
                    r = 1.0;
                    g = (v - 0.33) / 0.33;
                } else {
                    r = 1.0;
                    g = 1.0;
                    b = (v - 0.66) / 0.34;
                }
                return cv::Vec3b(b8(b), b8(g), b8(r));
            }
            case Palette::Plasma: {
                // simplified purple -> pink -> yellow
                double r = v;
                double g = std::max(0.0, (v - 0.5) * 2.0);
                double b = std::max(0.0, 0.5 - 0.5 * v);
                return cv::Vec3b(b8(b), b8(g), b8(r));
            }
            case Palette::Turbo: {
                // simplified blue -> cyan -> green -> yellow -> red
                double r = 0.0, g = 0.0, b = 0.0;
                if (v < 0.25) {
                    b = 1.0;
                    g = v / 0.25;
                } else if (v < 0.5) {
                    b = 1.0 - (v - 0.25) / 0.25;
                    g = 1.0;
                } else if (v < 0.75) {
                    g = 1.0;
                    r = (v - 0.5) / 0.25;
                } else {
                    r = 1.0;
                    g = 1.0 - (v - 0.75) / 0.25;
                }
                return cv::Vec3b(b8(b), b8(g), b8(r));
            }
        }
        return cv::Vec3b(0, 0, 0);
    }

    int width_;
    int height_;
    Channels channels_;
    Metavision::timestamp decay_time_us_;
    Palette palette_;
    int refresh_rate_hz_;
    std::vector<Metavision::timestamp> ts_buf_;   // merged buffer
    std::vector<Metavision::timestamp> ts_on_;    // ON-only buffer (split mode)
    std::vector<Metavision::timestamp> ts_off_;   // OFF-only buffer (split mode)
    Metavision::timestamp current_t_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_TIME_SURFACE_H
