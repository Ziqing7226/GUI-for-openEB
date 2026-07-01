// algo/cv/line_segment_detector.h — ELiSeD event-level line segment detection.
//
// Implements design §4.3.13 (ELiSeD, Cartucho 2018 IROS; see jAER ELiSeD).
// Reuses the 4-orientation filter (0°/45°/90°/135°) from §4.3.7 to classify
// each event via a 3x3 time-surface neighbourhood, accumulates
// orientation-consistent events into line candidates along the dominant
// direction, and fits segments via least-squares (PCA) line fitting with
// endpoint extraction and ID association for tracking. Header-only.

#ifndef GUI_ALGO_CV_LINE_SEGMENT_DETECTOR_H
#define GUI_ALGO_CV_LINE_SEGMENT_DETECTOR_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief Detected line segment with persistent tracking id.
struct LineSegment {
    cv::Point2f start;
    cv::Point2f end;
    float angle{0.0f};    ///< Orientation in degrees, [0, 180).
    int track_id{-1};     ///< Persistent track id, -1 if untracked.
};

/// @brief ELiSeD line segment detector (design §4.3.13).
class LineSegmentDetector {
public:
    /// @brief Orientation computation window (us); neighbour timestamps older
    /// than this are treated as stale and the orientation is rejected.
    static constexpr Metavision::timestamp kOrientationWindowUs = 10000;

    LineSegmentDetector(int width, int height,
                        int min_line_length_px = 20,
                        float orientation_threshold = 0.7f,
                        int max_line_gap_px = 5)
        : width_(width), height_(height),
          min_line_length_px_(min_line_length_px),
          orientation_threshold_(orientation_threshold),
          max_line_gap_px_(max_line_gap_px),
          last_t_(static_cast<std::size_t>(width) * height, -1) {}

    /// @brief Processes an event packet and returns detected line segments.
    std::vector<LineSegment> process(const EventPacket& packet) {
        std::vector<LineSegment> result;
        if (packet.empty()) return result;

        // Per-orientation accumulation of oriented events.
        std::vector<std::vector<cv::Point2f>> buckets(4);
        for (const Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            const std::size_t idx =
                static_cast<std::size_t>(e.y) * width_ + e.x;
            last_t_[idx] = e.t;

            int orient = -1;
            float conf = 0.0f;
            compute_orientation(e.x, e.y, orient, conf);
            if (orient < 0 || conf < orientation_threshold_) continue;
            buckets[static_cast<std::size_t>(orient)].emplace_back(
                static_cast<float>(e.x), static_cast<float>(e.y));
        }

        for (int o = 0; o < 4; ++o) {
            if (buckets[static_cast<std::size_t>(o)].size() <
                static_cast<std::size_t>(min_line_length_px_)) {
                continue;
            }
            LineSegment seg;
            if (fit_line(buckets[static_cast<std::size_t>(o)], seg)) {
                seg.track_id = associate(seg);
                result.push_back(seg);
            }
        }
        return result;
    }

    // Parameter accessors ---------------------------------------------------
    int min_line_length_px() const { return min_line_length_px_; }
    float orientation_threshold() const { return orientation_threshold_; }
    int max_line_gap_px() const { return max_line_gap_px_; }
    void set_min_line_length_px(int v) { min_line_length_px_ = v; }
    void set_orientation_threshold(float v) { orientation_threshold_ = v; }
    void set_max_line_gap_px(int v) { max_line_gap_px_ = v; }

    void reset() {
        std::fill(last_t_.begin(), last_t_.end(),
                  static_cast<Metavision::timestamp>(-1));
        tracks_.clear();
        next_track_id_ = 0;
    }

private:
    struct Track {
        int id{-1};
        cv::Point2f last_start;
        cv::Point2f last_end;
    };

    static float dist2(const cv::Point2f& a, const cv::Point2f& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    // 4 orientations (0=0°, 1=45°, 2=90°, 3=135°). For each, the opposing
    // neighbour pair perpendicular to the edge direction; the pair with the
    // smallest timestamp difference indicates the dominant edge orientation.
    void compute_orientation(std::uint16_t x, std::uint16_t y,
                             int& orient, float& conf) const {
        orient = -1;
        conf = 0.0f;
        const int px = static_cast<int>(x);
        const int py = static_cast<int>(y);
        static constexpr int pa[4][2] = {{0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
        static constexpr int pb[4][2] = {{0,  1}, { 1,  1}, { 1, 0}, { 1,-1}};
        Metavision::timestamp best_diff = 0;
        bool found = false;
        for (int o = 0; o < 4; ++o) {
            const int ax = px + pa[o][0], ay = py + pa[o][1];
            const int bx = px + pb[o][0], by = py + pb[o][1];
            if (ax < 0 || ay < 0 || bx < 0 || by < 0) continue;
            if (ax >= width_ || ay >= height_ || bx >= width_ || by >= height_) continue;
            const Metavision::timestamp ta =
                last_t_[static_cast<std::size_t>(ay) * width_ + ax];
            const Metavision::timestamp tb =
                last_t_[static_cast<std::size_t>(by) * width_ + bx];
            if (ta < 0 || tb < 0) continue;
            const Metavision::timestamp diff = ta > tb ? ta - tb : tb - ta;
            if (!found || diff < best_diff) {
                best_diff = diff;
                orient = o;
                found = true;
            }
        }
        if (!found) return;
        const float norm = static_cast<float>(best_diff) /
                           static_cast<float>(kOrientationWindowUs);
        conf = norm >= 1.0f ? 0.0f : 1.0f - norm;
    }

    /// @brief Least-squares (PCA) line fit with endpoint extraction.
    bool fit_line(const std::vector<cv::Point2f>& pts, LineSegment& seg) const {
        const std::size_t n = pts.size();
        if (n < static_cast<std::size_t>(min_line_length_px_)) return false;
        double sx = 0.0, sy = 0.0;
        for (const auto& p : pts) { sx += p.x; sy += p.y; }
        const double mx = sx / static_cast<double>(n);
        const double my = sy / static_cast<double>(n);
        double sxx = 0.0, sxy = 0.0, syy = 0.0;
        for (const auto& p : pts) {
            const double dx = p.x - mx;
            const double dy = p.y - my;
            sxx += dx * dx;
            sxy += dx * dy;
            syy += dy * dy;
        }
        const double angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
        const double dx = std::cos(angle);
        const double dy = std::sin(angle);
        double tmin = 0.0, tmax = 0.0;
        bool first = true;
        for (const auto& p : pts) {
            const double t = (p.x - mx) * dx + (p.y - my) * dy;
            if (first) { tmin = t; tmax = t; first = false; }
            else { if (t < tmin) tmin = t; if (t > tmax) tmax = t; }
        }
        const double length = tmax - tmin;
        if (length < static_cast<double>(min_line_length_px_)) return false;
        seg.start = cv::Point2f(static_cast<float>(mx + dx * tmin),
                                static_cast<float>(my + dy * tmin));
        seg.end   = cv::Point2f(static_cast<float>(mx + dx * tmax),
                                static_cast<float>(my + dy * tmax));
        float deg = static_cast<float>(angle * 180.0 / CV_PI);
        if (deg < 0.0f) deg += 180.0f;
        seg.angle = deg;
        return true;
    }

    /// @brief Associates a segment with an existing track by nearest endpoint.
    int associate(const LineSegment& seg) {
        const float tol = static_cast<float>(max_line_gap_px_);
        const float tol2 = tol * tol;
        int best_id = -1;
        float best_d2 = tol2;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const float d = std::min(std::min(dist2(tracks_[i].last_start, seg.start),
                                              dist2(tracks_[i].last_end,   seg.end)),
                                     std::min(dist2(tracks_[i].last_start, seg.end),
                                              dist2(tracks_[i].last_end,   seg.start)));
            if (d < best_d2) { best_d2 = d; best_id = tracks_[i].id; }
        }
        if (best_id < 0) {
            best_id = next_track_id_++;
            tracks_.push_back(Track{best_id, seg.start, seg.end});
        } else {
            for (auto& tr : tracks_) {
                if (tr.id == best_id) { tr.last_start = seg.start; tr.last_end = seg.end; break; }
            }
        }
        return best_id;
    }

    int width_;
    int height_;
    int min_line_length_px_;
    float orientation_threshold_;
    int max_line_gap_px_;
    std::vector<Metavision::timestamp> last_t_;
    std::vector<Track> tracks_;
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_LINE_SEGMENT_DETECTOR_H
