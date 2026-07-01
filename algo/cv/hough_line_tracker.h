// algo/cv/hough_line_tracker.h — Hough line tracking on accumulated event frames.
//
// Implements design §4.3.14 (jAER HoughLineTracker). Accumulates events into a
// binary count frame over a configurable time window, then runs the
// probabilistic Hough transform (cv::HoughLinesP) to detect line segments.
// Detected segments are associated to persistent tracks by endpoint proximity.
// Header-only.

#ifndef GUI_ALGO_CV_HOUGH_LINE_TRACKER_H
#define GUI_ALGO_CV_HOUGH_LINE_TRACKER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief Detected Hough line segment with persistent tracking id.
struct HoughLine {
    cv::Point2f start;
    cv::Point2f end;
    float angle{0.0f};    ///< Orientation in degrees, [0, 180).
    int track_id{-1};     ///< Persistent track id, -1 if untracked.
};

/// @brief Hough line tracker using cv::HoughLinesP on accumulated event frames.
class HoughLineTracker {
public:
    HoughLineTracker(int width, int height,
                     float accumulation_ms = 10.0f,
                     int hough_threshold = 50,
                     int min_line_length_px = 30,
                     int max_line_gap_px = 5)
        : width_(width), height_(height),
          accumulation_us_(static_cast<Metavision::timestamp>(
              accumulation_ms * 1000.0f)),
          hough_threshold_(hough_threshold),
          min_line_length_px_(min_line_length_px),
          max_line_gap_px_(max_line_gap_px),
          accum_(cv::Mat::zeros(height, width, CV_8UC1)) {}

    /// @brief Accumulates events and, on window expiry, runs Hough detection.
    std::vector<HoughLine> process(const EventPacket& packet) {
        std::vector<HoughLine> result;
        if (packet.empty()) return result;
        for (const Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            if (window_start_ < 0) window_start_ = e.t;
            auto& v = accum_.at<std::uint8_t>(static_cast<int>(e.y),
                                              static_cast<int>(e.x));
            if (v < 255) ++v;
            last_t_ = e.t;
        }
        if (window_start_ >= 0 && last_t_ - window_start_ >= accumulation_us_) {
            std::vector<cv::Vec4i> lines;
            cv::HoughLinesP(accum_, lines, 1.0, CV_PI / 180.0,
                            hough_threshold_,
                            static_cast<double>(min_line_length_px_),
                            static_cast<double>(max_line_gap_px_));
            for (const auto& l : lines) {
                HoughLine hl;
                hl.start = cv::Point2f(static_cast<float>(l[0]),
                                       static_cast<float>(l[1]));
                hl.end   = cv::Point2f(static_cast<float>(l[2]),
                                       static_cast<float>(l[3]));
                const float dx = hl.end.x - hl.start.x;
                const float dy = hl.end.y - hl.start.y;
                float deg = static_cast<float>(std::atan2(dy, dx) * 180.0 / CV_PI);
                if (deg < 0.0f) deg += 180.0f;
                hl.angle = deg;
                hl.track_id = associate(hl);
                result.push_back(hl);
            }
            accum_.setTo(0);
            window_start_ = last_t_;
        }
        return result;
    }

    // Parameter accessors ---------------------------------------------------
    float accumulation_ms() const {
        return static_cast<float>(accumulation_us_) / 1000.0f;
    }
    int hough_threshold() const { return hough_threshold_; }
    int min_line_length_px() const { return min_line_length_px_; }
    int max_line_gap_px() const { return max_line_gap_px_; }
    void set_accumulation_ms(float v) {
        accumulation_us_ = static_cast<Metavision::timestamp>(v * 1000.0f);
    }
    void set_hough_threshold(int v) { hough_threshold_ = v; }
    void set_min_line_length_px(int v) { min_line_length_px_ = v; }
    void set_max_line_gap_px(int v) { max_line_gap_px_ = v; }

    void reset() {
        accum_.setTo(0);
        window_start_ = -1;
        last_t_ = -1;
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

    int associate(const HoughLine& hl) {
        const float tol = static_cast<float>(max_line_gap_px_);
        const float tol2 = tol * tol;
        int best_id = -1;
        float best_d2 = tol2;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const float d = std::min(std::min(dist2(tracks_[i].last_start, hl.start),
                                              dist2(tracks_[i].last_end,   hl.end)),
                                     std::min(dist2(tracks_[i].last_start, hl.end),
                                              dist2(tracks_[i].last_end,   hl.start)));
            if (d < best_d2) { best_d2 = d; best_id = tracks_[i].id; }
        }
        if (best_id < 0) {
            best_id = next_track_id_++;
            tracks_.push_back(Track{best_id, hl.start, hl.end});
        } else {
            for (auto& tr : tracks_) {
                if (tr.id == best_id) { tr.last_start = hl.start; tr.last_end = hl.end; break; }
            }
        }
        return best_id;
    }

    int width_;
    int height_;
    Metavision::timestamp accumulation_us_;
    int hough_threshold_;
    int min_line_length_px_;
    int max_line_gap_px_;
    cv::Mat accum_;
    Metavision::timestamp window_start_{-1};
    Metavision::timestamp last_t_{-1};
    std::vector<Track> tracks_;
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_HOUGH_LINE_TRACKER_H
