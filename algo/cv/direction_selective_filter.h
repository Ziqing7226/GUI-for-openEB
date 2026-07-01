// algo/cv/direction_selective_filter.h — 8-direction motion labelling.
//
// Inspired by jAER AbstractDirectionSelectiveFilter (design §4.3.8). For each
// event the 8-neighbourhood most-recent timestamps are inspected; the neighbour
// with the smallest positive recency (t - t_neighbour) within the time window
// indicates the direction of motion. An optional global mode accumulates a
// direction histogram over the batch to report the dominant scene motion.
// Header-only.

#ifndef GUI_ALGO_CV_DIRECTION_SELECTIVE_FILTER_H
#define GUI_ALGO_CV_DIRECTION_SELECTIVE_FILTER_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief Labels events with one of 8 motion directions from a time surface.
class DirectionSelectiveFilter {
public:
    static constexpr int kNumDirections = 8;

    /// @brief Direction index in [0,7]: 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE.
    /// -1 = undetermined.
    DirectionSelectiveFilter(int width, int height)
        : width_(width), height_(height),
          surface_(static_cast<std::size_t>(width) * height, 0) {
        global_hist_.fill(0);
    }

    void set_time_window_us(int v) { time_window_us_ = clamp_i(v, 1000, 50000); }
    void set_enable_global_mode(bool v) { enable_global_mode_ = v; }

    int time_window_us() const { return time_window_us_; }
    bool enable_global_mode() const { return enable_global_mode_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Classifies a single event, updating the internal time surface.
    /// @return Direction index in [0,7] or -1 if no recent supporting neighbour.
    int classify(const Event& e) {
        int dir = -1;
        if (e.x < width_ && e.y < height_) {
            dir = compute_direction(e);
            surface_[idx_of(e.x, e.y)] = e.t;
            if (enable_global_mode_ && dir >= 0) ++global_hist_[dir];
        }
        return dir;
    }

    /// @brief Classifies a batch; fills @p out (resized to @p count).
    void process(const Event* events, std::size_t count, std::vector<int>& out) {
        out.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = classify(events[i]);
        }
    }

    /// @brief Processes an event packet (updates state + global histogram).
    void process(EventPacket& events) {
        for (const auto& e : events) classify(e);
    }

    /// @brief Returns the dominant global direction over the current batch, or
    /// -1 if global mode is disabled / no events recorded.
    int global_direction() const {
        if (!enable_global_mode_) return -1;
        int best = -1, best_count = 0;
        for (int i = 0; i < kNumDirections; ++i) {
            if (global_hist_[i] > best_count) {
                best_count = global_hist_[i];
                best = i;
            }
        }
        return best;
    }

    const std::array<int, kNumDirections>& global_histogram() const {
        return global_hist_;
    }

    /// @brief Angle (deg) for a direction index, or -1.
    static float direction_angle(int dir) {
        if (dir < 0 || dir >= kNumDirections) return -1.0F;
        return static_cast<float>(dir) * 45.0F;
    }

    void reset() {
        std::fill(surface_.begin(), surface_.end(), 0);
        global_hist_.fill(0);
    }

private:
    static int clamp_i(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    std::size_t idx_of(int x, int y) const {
        return static_cast<std::size_t>(y) * width_ + x;
    }

    int compute_direction(const Event& e) const {
        // 8 neighbour offsets: E, NE, N, NW, W, SW, S, SE.
        static const int dx[kNumDirections] = {1, 1, 0, -1, -1, -1, 0, 1};
        static const int dy[kNumDirections] = {0, -1, -1, -1, 0, 1, 1, 1};
        const Metavision::timestamp win = time_window_us_;
        int best_dir = -1;
        Metavision::timestamp best_recency = win;
        for (int d = 0; d < kNumDirections; ++d) {
            const int nx = e.x + dx[d];
            const int ny = e.y + dy[d];
            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
            const Metavision::timestamp lt = surface_[idx_of(nx, ny)];
            if (lt == 0) continue;
            const Metavision::timestamp rec = e.t - lt;
            if (rec < 0 || rec > win) continue;
            if (rec < best_recency) {
                best_recency = rec;
                best_dir = d;
            }
        }
        return best_dir;
    }

    int width_;
    int height_;
    int time_window_us_{10000};
    bool enable_global_mode_{true};
    std::vector<Metavision::timestamp> surface_;
    std::array<int, kNumDirections> global_hist_{};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_DIRECTION_SELECTIVE_FILTER_H
