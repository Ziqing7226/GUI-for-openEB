// algo/cv/hinge_line_tracker.h — Hinge (corner) detection from line pairs.
//
// Implements design §4.3.16 (jAER hinge project). A hinge is the intersection
// point of two line segments meeting at an angle. Line detection reuses the
// HoughLineTracker (§4.3.14); pairs whose mutual angle is within
// angle_tolerance_deg of 90° (a fold / crease) are kept, and their geometric
// intersection is reported as a hinge point with persistent tracking id.
// Header-only.

#ifndef GUI_ALGO_CV_HINGE_LINE_TRACKER_H
#define GUI_ALGO_CV_HINGE_LINE_TRACKER_H

#include <cmath>
#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"
#include "algo/cv/hough_line_tracker.h"

namespace gui_algo {

/// @brief Detected hinge (intersection of two line segments).
struct Hinge {
    cv::Point2f position;   ///< Intersection point of the two segments.
    float angle1{0.0f};     ///< Orientation of the first segment (degrees).
    float angle2{0.0f};     ///< Orientation of the second segment (degrees).
    int track_id{-1};       ///< Persistent track id, -1 if untracked.
};

/// @brief Hinge detector reusing HoughLineTracker for line detection.
class HingeLineTracker {
public:
    HingeLineTracker(int width, int height,
                     float accumulation_ms = 10.0f,
                     int hough_threshold = 50,
                     int min_line_length_px = 30,
                     int max_line_gap_px = 5,
                     float angle_tolerance_deg = 5.0f)
        : angle_tolerance_deg_(angle_tolerance_deg),
          lines_(width, height, accumulation_ms, hough_threshold,
                 min_line_length_px, max_line_gap_px),
          track_tol_px_(static_cast<float>(max_line_gap_px) * 2.0f) {}

    /// @brief Processes events and returns hinge points (only on window expiry;
    /// empty otherwise, matching the underlying HoughLineTracker cadence).
    std::vector<Hinge> process(const EventPacket& packet) {
        std::vector<Hinge> result;
        const std::vector<HoughLine> lines = lines_.process(packet);
        // Compare all line pairs; keep near-perpendicular intersections.
        for (std::size_t i = 0; i < lines.size(); ++i) {
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                const float between = angle_between(lines[i].angle, lines[j].angle);
                const float dev = std::abs(between - 90.0f);
                if (dev > angle_tolerance_deg_) continue;
                cv::Point2f pt;
                if (!intersect(lines[i].start, lines[i].end,
                               lines[j].start, lines[j].end, pt)) {
                    continue;
                }
                Hinge h;
                h.position = pt;
                h.angle1 = lines[i].angle;
                h.angle2 = lines[j].angle;
                h.track_id = associate(h);
                result.push_back(h);
            }
        }
        return result;
    }

    // Parameter accessors ---------------------------------------------------
    float angle_tolerance_deg() const { return angle_tolerance_deg_; }
    void set_angle_tolerance_deg(float v) { angle_tolerance_deg_ = v; }

    void reset() {
        lines_.reset();
        tracks_.clear();
        next_track_id_ = 0;
    }

private:
    struct Track {
        int id{-1};
        cv::Point2f last_position;
    };

    /// @brief Smaller angle between two orientations in degrees, in [0, 90].
    static float angle_between(float a1, float a2) {
        float d = a1 - a2;
        if (d < 0.0f) d = -d;
        if (d >= 90.0f) d = 180.0f - d;
        return d;
    }

    /// @brief Line-line intersection of segments (p1p2) and (p3p4).
    /// @return false if the lines are (near) parallel.
    static bool intersect(const cv::Point2f& p1, const cv::Point2f& p2,
                          const cv::Point2f& p3, const cv::Point2f& p4,
                          cv::Point2f& out) {
        const float denom = (p1.x - p2.x) * (p3.y - p4.y) -
                            (p1.y - p2.y) * (p3.x - p4.x);
        if (std::abs(denom) < 1e-6f) return false;
        const float a = p1.x * p2.y - p1.y * p2.x;
        const float b = p3.x * p4.y - p3.y * p4.x;
        out.x = (a * (p3.x - p4.x) - (p1.x - p2.x) * b) / denom;
        out.y = (a * (p3.y - p4.y) - (p1.y - p2.y) * b) / denom;
        return true;
    }

    int associate(const Hinge& h) {
        const float tol2 = track_tol_px_ * track_tol_px_;
        int best_id = -1;
        float best_d2 = tol2;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const float dx = tracks_[i].last_position.x - h.position.x;
            const float dy = tracks_[i].last_position.y - h.position.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_id = tracks_[i].id; }
        }
        if (best_id < 0) {
            best_id = next_track_id_++;
            tracks_.push_back(Track{best_id, h.position});
        } else {
            for (auto& tr : tracks_) {
                if (tr.id == best_id) { tr.last_position = h.position; break; }
            }
        }
        return best_id;
    }

    float angle_tolerance_deg_;
    HoughLineTracker lines_;
    float track_tol_px_;
    std::vector<Track> tracks_;
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_HINGE_LINE_TRACKER_H
