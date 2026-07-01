// algo/cv/ultra_slow_motion.h — Ultra slow-motion replay via time dilation.
//
// Design §4.3.24. Replays events with dilated timestamps to achieve an
// equivalent frame rate >= 200,000 fps. Replaces the main display area (no
// overlay). Events are time-stretched by a configurable dilation factor and
// accumulated over a minimum window (default 5 us = 200,000 fps) for rendering.
// Inspired by jAER slow-motion replay utilities. Header-only.

#ifndef GUI_ALGO_CV_ULTRA_SLOW_MOTION_H
#define GUI_ALGO_CV_ULTRA_SLOW_MOTION_H

#include <cstddef>
#include <vector>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"

namespace gui_algo {

/// @brief Time-dilation replay producing events with stretched timestamps.
///
/// Each event timestamp is remapped as
///   t_out = t_base + (t_in - t_base) * dilation_factor
/// so that the original event stream is slowed down by @p dilation_factor for
/// replay. The minimum accumulation window (@p min_accumulation_us) defines the
/// equivalent output frame period: 5 us corresponds to 200,000 fps equivalent.
class UltraSlowMotion {
public:
    /// @brief Constructs the dilator.
    /// @param dilation_factor Time stretch factor in [1, 10000], default 10.
    /// @param min_accumulation_us Equivalent frame period in us, [1, 1000],
    ///        default 5 (= 200,000 fps).
    UltraSlowMotion(float dilation_factor = 10.0f, int min_accumulation_us = 5)
        : dilation_factor_(clamp_dilation(dilation_factor)),
          min_accumulation_us_(clamp_accumulation(min_accumulation_us)) {}

    /// @brief Sets the time dilation factor (clamped to [1, 10000]).
    void set_dilation_factor(float f) { dilation_factor_ = clamp_dilation(f); }
    float dilation_factor() const { return dilation_factor_; }

    /// @brief Sets the equivalent frame period in us (clamped to [1, 1000]).
    void set_min_accumulation_us(int us) {
        min_accumulation_us_ = clamp_accumulation(us);
    }
    int min_accumulation_us() const { return min_accumulation_us_; }

    /// @brief Dilates event timestamps by @p dilation_factor relative to the
    ///        stream base timestamp (first event ever seen).
    /// @return Vector of events with adjusted (stretched) timestamps.
    std::vector<Event> process(const Event* events, std::size_t n) {
        std::vector<Event> out;
        if (events == nullptr || n == 0) return out;
        out.reserve(n);
        if (!base_set_) {
            base_t_ = events[0].t;
            base_set_ = true;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const Event& e = events[i];
            Event d = e;
            const double delta = static_cast<double>(e.t - base_t_);
            d.t = base_t_ + static_cast<Metavision::timestamp>(
                                delta * static_cast<double>(dilation_factor_));
            out.push_back(d);
        }
        return out;
    }

    /// @brief Equivalent output frame rate (fps) before dilation.
    /// 5 us period => 200,000 fps equivalent granularity.
    double equivalent_fps() const {
        return 1.0e6 / static_cast<double>(min_accumulation_us_);
    }

    /// @brief Resets the stream base timestamp so the next batch re-anchors.
    void reset() {
        base_set_ = false;
        base_t_ = 0;
    }

private:
    static float clamp_dilation(float f) {
        if (f < 1.0f) return 1.0f;
        if (f > 10000.0f) return 10000.0f;
        return f;
    }
    static int clamp_accumulation(int us) {
        if (us < 1) return 1;
        if (us > 1000) return 1000;
        return us;
    }

    float dilation_factor_;
    int min_accumulation_us_;
    bool base_set_{false};
    Metavision::timestamp base_t_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_ULTRA_SLOW_MOTION_H
