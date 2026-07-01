// gui/display/adaptive_renderer.h — adaptive intensity renderer.
//
// Design §1.6.6 (jAER AdaptiveIntensityRenderer). Produces a QImage whose
// per-pixel gray value is a function of dt — the time elapsed since the last
// event at that pixel. Pixels that fired recently are bright; pixels that
// have been quiet decay toward black. An optional per-pixel calibration
// matrix can rescale the decay (identity by default).
//
// This is a pure data→QImage renderer; it does not own a widget. The
// EventDisplayWidget uploads the produced QImage to its GL texture.
//
// Header-only.

#ifndef GUI_DISPLAY_ADAPTIVE_RENDERER_H
#define GUI_DISPLAY_ADAPTIVE_RENDERER_H

#include <QColor>
#include <QImage>
#include <cstdint>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/utils/timestamp.h>

namespace gui {

/// @brief Renders events as an adaptive-decay intensity image.
///
/// Intensity model: gray = 255 * exp(-dt / tau_us), where dt is the time
/// since the last event at that pixel and tau_us is the decay time constant.
/// An optional calibration matrix (one float per pixel, default 1.0) scales
/// the effective tau per pixel (e.g. to compensate for pixel-gain variation).
class AdaptiveRenderer {
public:
    /// @brief Constructs the renderer.
    /// @param width,height Sensor geometry in pixels.
    /// @param tau_us Decay time constant in microseconds.
    /// @param on_color Bright color for recently-fired ON events.
    /// @param off_color Bright color for recently-fired OFF events.
    AdaptiveRenderer(int width, int height,
                     Metavision::timestamp tau_us = 50000,
                     QColor on_color = QColor(255, 255, 255),
                     QColor off_color = QColor(255, 255, 255))
        : width_(width),
          height_(height),
          tau_us_(tau_us <= 0 ? 1 : tau_us),
          on_color_(on_color),
          off_color_(off_color),
          last_ts_(static_cast<std::size_t>(width) * height, -1),
          last_pol_(static_cast<std::size_t>(width) * height, 0),
          calib_(static_cast<std::size_t>(width) * height, 1.0f) {}

    /// @brief Sets the per-pixel calibration matrix (gain per pixel).
    /// Must have width*height entries; values clip to (0, 10].
    void set_calibration(const std::vector<float>& gains) {
        const std::size_t n = static_cast<std::size_t>(width_) * height_;
        calib_.assign(n, 1.0f);
        const std::size_t m = std::min(gains.size(), n);
        for (std::size_t i = 0; i < m; ++i) {
            float g = gains[i];
            if (!(g > 0.0f)) g = 1.0f;
            if (g > 10.0f) g = 10.0f;
            calib_[i] = g;
        }
    }

    /// @brief Resets the per-pixel calibration to identity (all 1.0).
    void clear_calibration() {
        std::fill(calib_.begin(), calib_.end(), 1.0f);
    }

    void set_tau_us(Metavision::timestamp tau) {
        tau_us_ = (tau <= 0) ? 1 : tau;
    }
    Metavision::timestamp tau_us() const { return tau_us_; }

    /// @brief Sets the reference "now" timestamp (e.g. last received event).
    /// Used as the upper bound for dt in render().
    void set_now(Metavision::timestamp now) { now_ = now; }

    /// @brief Feeds a batch of events; updates per-pixel last-event state.
    void process(const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        if (begin == nullptr || end == nullptr || begin >= end) return;
        for (const Metavision::EventCD* e = begin; e != end; ++e) {
            // EventCD::x/y are unsigned; only the upper bound can fail.
            if (e->x >= width_ || e->y >= height_) continue;
            const std::size_t idx = static_cast<std::size_t>(e->y) * width_ + e->x;
            last_ts_[idx] = e->t;
            last_pol_[idx] = static_cast<std::int8_t>(e->p ? 1 : -1);
            if (e->t > now_) now_ = e->t;
        }
    }

    /// @brief Clears all per-pixel state (e.g. on disconnect / new source).
    void clear() {
        std::fill(last_ts_.begin(), last_ts_.end(), -1);
        std::fill(last_pol_.begin(), last_pol_.end(), 0);
        std::fill(calib_.begin(), calib_.end(), 1.0f);
        now_ = 0;
    }

    /// @brief Renders the current state into a Format_RGBA8888 QImage.
    /// Pixels that never fired are black; otherwise brightness follows the
    /// decay model. When on/off colors differ, polarity tints the decay.
    QImage render() const {
        QImage img(width_, height_, QImage::Format_RGBA8888);
        img.fill(Qt::black);
        if (width_ <= 0 || height_ <= 0) return img;

        const std::size_t n = static_cast<std::size_t>(width_) * height_;
        const bool tinted = (on_color_ != off_color_);
        for (std::size_t i = 0; i < n; ++i) {
            const Metavision::timestamp ts = last_ts_[i];
            if (ts < 0) continue;
            const Metavision::timestamp dt = now_ - ts;
            float decay = 1.0f;
            if (dt > 0) {
                const float eff_tau = static_cast<float>(tau_us_) * calib_[i];
                decay = std::exp(-static_cast<float>(dt) / eff_tau);
                if (decay < 0.0f) decay = 0.0f;
                if (decay > 1.0f) decay = 1.0f;
            }
            const int x = static_cast<int>(i % static_cast<std::size_t>(width_));
            const int y = static_cast<int>(i / static_cast<std::size_t>(width_));
            QColor base = tinted ? (last_pol_[i] > 0 ? on_color_ : off_color_)
                                 : on_color_;
            QRgb c = qRgba(static_cast<int>(base.red() * decay),
                           static_cast<int>(base.green() * decay),
                           static_cast<int>(base.blue() * decay),
                           255);
            img.setPixel(x, y, c);
        }
        return img;
    }

private:
    int width_{0};
    int height_{0};
    Metavision::timestamp tau_us_{50000};
    QColor on_color_;
    QColor off_color_;
    Metavision::timestamp now_{0};
    std::vector<Metavision::timestamp> last_ts_;   ///< last event time per pixel, -1 = none
    std::vector<std::int8_t> last_pol_;            ///< last polarity per pixel (+1/-1/0)
    std::vector<float> calib_;                     ///< per-pixel gain (default 1.0)
};

} // namespace gui

#endif // GUI_DISPLAY_ADAPTIVE_RENDERER_H
