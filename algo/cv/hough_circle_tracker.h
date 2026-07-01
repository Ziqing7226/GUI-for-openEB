// algo/cv/hough_circle_tracker.h — Hough circle tracking + particle filter mode.
//
// Implements design §4.3.15 (jAER HoughCircleTracker). Accumulates events into
// a count frame and runs cv::HoughCircles to detect circles. Also supports a
// ParticleFilter mode that tracks a single circular target with a Monte Carlo
// particle filter (algo/common/particle_filter.h), corresponding to the
// circle_tracker.h variant. Header-only.

#ifndef GUI_ALGO_CV_HOUGH_CIRCLE_TRACKER_H
#define GUI_ALGO_CV_HOUGH_CIRCLE_TRACKER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"
#include "algo/common/particle_filter.h"

namespace gui_algo {

/// @brief Circle detection mode.
enum class HoughCircleMode {
    Hough,          ///< cv::HoughCircles on accumulated frames.
    ParticleFilter  ///< Monte Carlo particle-filter tracking.
};

/// @brief Detected circle with persistent tracking id.
struct HoughCircle {
    cv::Point2f center;
    float radius{0.0f};
    int track_id{-1};   ///< Persistent track id, -1 if untracked.
};

/// @brief Hough circle tracker using cv::HoughCircles or a particle filter.
class HoughCircleTracker {
public:
    HoughCircleTracker(int width, int height,
                       float accumulation_ms = 10.0f,
                       int min_radius_px = 5,
                       int max_radius_px = 50,
                       int hough_threshold = 30,
                       HoughCircleMode mode = HoughCircleMode::Hough)
        : width_(width), height_(height),
          accumulation_us_(static_cast<Metavision::timestamp>(
              accumulation_ms * 1000.0f)),
          min_radius_px_(min_radius_px),
          max_radius_px_(max_radius_px),
          hough_threshold_(hough_threshold),
          mode_(mode),
          accum_(cv::Mat::zeros(height, width, CV_8UC1)),
          pf_(200, 3.0, 5.0, 0.033, 42) {}

    /// @brief Processes an event packet and returns detected circles.
    std::vector<HoughCircle> process(const EventPacket& packet) {
        std::vector<HoughCircle> result;
        if (packet.empty()) return result;
        if (mode_ == HoughCircleMode::ParticleFilter) {
            return process_particle(packet);
        }
        for (const Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            if (window_start_ < 0) window_start_ = e.t;
            auto& v = accum_.at<std::uint8_t>(static_cast<int>(e.y),
                                              static_cast<int>(e.x));
            if (v < 255) ++v;
            last_t_ = e.t;
        }
        if (window_start_ >= 0 && last_t_ - window_start_ >= accumulation_us_) {
            std::vector<cv::Vec3f> circles;
            const double min_dist =
                std::max(1.0, static_cast<double>(min_radius_px_));
            cv::HoughCircles(accum_, circles, cv::HOUGH_GRADIENT, 1.0, min_dist,
                             100.0, static_cast<double>(hough_threshold_),
                             min_radius_px_, max_radius_px_);
            for (const auto& c : circles) {
                HoughCircle hc;
                hc.center = cv::Point2f(c[0], c[1]);
                hc.radius = c[2];
                hc.track_id = associate(hc);
                result.push_back(hc);
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
    int min_radius_px() const { return min_radius_px_; }
    int max_radius_px() const { return max_radius_px_; }
    int hough_threshold() const { return hough_threshold_; }
    HoughCircleMode mode() const { return mode_; }
    void set_accumulation_ms(float v) {
        accumulation_us_ = static_cast<Metavision::timestamp>(v * 1000.0f);
    }
    void set_min_radius_px(int v) { min_radius_px_ = v; }
    void set_max_radius_px(int v) { max_radius_px_ = v; }
    void set_hough_threshold(int v) { hough_threshold_ = v; }
    void set_mode(HoughCircleMode m) { mode_ = m; }

    void reset() {
        accum_.setTo(0);
        window_start_ = -1;
        last_t_ = -1;
        pf_inited_ = false;
        tracks_.clear();
        next_track_id_ = 0;
    }

private:
    struct Track {
        int id{-1};
        cv::Point2f last_center;
    };

    /// @brief Particle-filter mode: tracks the centroid of recent activity.
    std::vector<HoughCircle> process_particle(const EventPacket& packet) {
        std::vector<HoughCircle> result;
        double sx = 0.0, sy = 0.0;
        std::size_t c = 0;
        for (const Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            sx += e.x;
            sy += e.y;
            ++c;
        }
        if (c == 0) return result;
        const double mx = sx / static_cast<double>(c);
        const double my = sy / static_cast<double>(c);
        if (!pf_inited_) {
            pf_.init(mx, my);
            pf_inited_ = true;
        } else {
            pf_.predict();
            pf_.update(mx, my);
            pf_.resample();
        }
        const Point2D est = pf_.estimate();
        HoughCircle hc;
        hc.center = cv::Point2f(static_cast<float>(est.x),
                                static_cast<float>(est.y));
        hc.radius = static_cast<float>(min_radius_px_);
        hc.track_id = associate(hc);
        result.push_back(hc);
        return result;
    }

    int associate(const HoughCircle& hc) {
        const float tol = static_cast<float>(max_radius_px_);
        const float tol2 = tol * tol;
        int best_id = -1;
        float best_d2 = tol2;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const float dx = tracks_[i].last_center.x - hc.center.x;
            const float dy = tracks_[i].last_center.y - hc.center.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_id = tracks_[i].id; }
        }
        if (best_id < 0) {
            best_id = next_track_id_++;
            tracks_.push_back(Track{best_id, hc.center});
        } else {
            for (auto& tr : tracks_) {
                if (tr.id == best_id) { tr.last_center = hc.center; break; }
            }
        }
        return best_id;
    }

    int width_;
    int height_;
    Metavision::timestamp accumulation_us_;
    int min_radius_px_;
    int max_radius_px_;
    int hough_threshold_;
    HoughCircleMode mode_;
    cv::Mat accum_;
    Metavision::timestamp window_start_{-1};
    Metavision::timestamp last_t_{-1};
    ParticleFilter pf_;
    bool pf_inited_{false};
    std::vector<Track> tracks_;
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_HOUGH_CIRCLE_TRACKER_H
