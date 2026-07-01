// algo/cv/corner_detector.h — event-native corner detection and tracking.
//
// Self-developed (design §4.3.12), inspired by jAER label/ corner detectors.
// Three modes:
//   EndStopped      — end-stopped response via min-eigenvalue of the local
//                     structure tensor (cv::cornerMinEigenVal).
//   TypeCoincidence — ON/OFF polarity co-occurrence in a spatiotemporal
//                     neighbourhood (jAER TypeCoincidenceFilter).
//   Harris          — frame-based Harris on the accumulation frame
//                     (cv::cornerHarris) mapped back to event positions.
// Detected corners are tracked by nearest-neighbour matching; tracks shorter
// than min_track_len are suppressed. Output: vector<Corner>. Header-only.

#ifndef GUI_ALGO_CV_CORNER_DETECTOR_H
#define GUI_ALGO_CV_CORNER_DETECTOR_H

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

/// @brief A detected and tracked corner.
struct Corner {
    float x{0.0F};
    float y{0.0F};
    float strength{0.0F};
    int track_id{-1};
    std::vector<cv::Point2f> trajectory;
};

/// @brief Multi-mode corner detector with nearest-neighbour tracking.
class CornerDetector {
public:
    enum class Mode { EndStopped, TypeCoincidence, Harris };

    CornerDetector(int width, int height, Mode mode = Mode::EndStopped)
        : width_(width), height_(height), mode_(mode) {
        reset();
    }

    // Parameters (defaults per design §4.3.12) ----------------------------
    void set_mode(Mode m) { mode_ = m; }
    void set_accumulation_ms(double v) { accumulation_ms_ = clamp_d(v, 1.0, 100.0); }
    void set_threshold(double v) { threshold_ = clamp_d(v, 1e-6, 1.0); }
    void set_track_radius_px(int v) { track_radius_px_ = clamp_i(v, 1, 30); }
    void set_min_track_len(int v) { min_track_len_ = clamp_i(v, 1, 100); }
    void set_output_hz(int v) { output_hz_ = clamp_i(v, 10, 500); }

    Mode mode() const { return mode_; }
    double accumulation_ms() const { return accumulation_ms_; }
    double threshold() const { return threshold_; }
    int track_radius_px() const { return track_radius_px_; }
    int min_track_len() const { return min_track_len_; }
    int output_hz() const { return output_hz_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Accumulates events; runs detection + tracking when the
    ///        accumulation window elapses, emitting corners at output_hz.
    void process(const Event* events, std::size_t count) {
        ensure_mats();
        for (std::size_t i = 0; i < count; ++i) {
            const Event& e = events[i];
            if (e.x >= width_ || e.y >= height_) continue;
            const int x = static_cast<int>(e.x);
            const int y = static_cast<int>(e.y);
            float& a = accum_.at<float>(y, x);
            if (a < 1e6F) a += 1.0F;
            if (e.p) {
                float& on = on_.at<float>(y, x);
                if (on < 1e6F) on += 1.0F;
            } else {
                float& off = off_.at<float>(y, x);
                if (off < 1e6F) off += 1.0F;
            }
            if (e.t > last_event_t_) last_event_t_ = e.t;
        }
        if (last_event_t_ - last_frame_t_ >= accum_us()) {
            detect_and_track();
            last_frame_t_ = last_event_t_;
        }
        if (last_event_t_ - last_emit_t_ >= emit_us()) {
            emit_corners();
            last_emit_t_ = last_event_t_;
        }
    }

    /// @brief Processes an event packet.
    void process(EventPacket& events) {
        process(events.data(), events.size());
    }

    /// @brief Returns the most recently emitted corners.
    const std::vector<Corner>& corners() const { return corners_; }

    void reset() {
        accum_ = cv::Mat();
        on_ = cv::Mat();
        off_ = cv::Mat();
        tracks_.clear();
        corners_.clear();
        last_event_t_ = 0;
        last_frame_t_ = 0;
        last_emit_t_ = 0;
        next_track_id_ = 0;
    }

private:
    static int clamp_i(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double clamp_d(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    Metavision::timestamp accum_us() const {
        return static_cast<Metavision::timestamp>(accumulation_ms_ * 1000.0);
    }
    Metavision::timestamp emit_us() const {
        return static_cast<Metavision::timestamp>(1e6 / static_cast<double>(output_hz_));
    }

    void ensure_mats() {
        if (accum_.empty()) {
            accum_ = cv::Mat::zeros(height_, width_, CV_32FC1);
            on_ = cv::Mat::zeros(height_, width_, CV_32FC1);
            off_ = cv::Mat::zeros(height_, width_, CV_32FC1);
        }
    }

    struct Detected { float x, y, strength; };

    void detect_and_track() {
        std::vector<Detected> detected;
        switch (mode_) {
            case Mode::EndStopped:      detect_min_eigenval(detected); break;
            case Mode::TypeCoincidence: detect_type_coincidence(detected); break;
            case Mode::Harris:          detect_harris(detected); break;
        }
        track(detected);
        // Reset accumulation frames after detection.
        accum_.setTo(0.0F);
        on_.setTo(0.0F);
        off_.setTo(0.0F);
    }

    void detect_min_eigenval(std::vector<Detected>& out) {
        cv::Mat strength;
        cv::cornerMinEigenVal(accum_, strength, 3, 3);
        cv::normalize(strength, strength, 0.0, 1.0, cv::NORM_MINMAX);
        collect_maxima(strength, out);
    }

    void detect_harris(std::vector<Detected>& out) {
        cv::Mat strength;
        cv::cornerHarris(accum_, strength, 3, 3, 0.04);
        cv::normalize(strength, strength, 0.0, 1.0, cv::NORM_MINMAX);
        collect_maxima(strength, out);
    }

    void detect_type_coincidence(std::vector<Detected>& out) {
        cv::Mat strength(height_, width_, CV_32FC1);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const float on = on_.at<float>(y, x);
                const float off = off_.at<float>(y, x);
                const float s = (on + off > 0.0F)
                    ? (2.0F * std::min(on, off) / (on + off))
                    : 0.0F;
                strength.at<float>(y, x) = s;
            }
        }
        collect_maxima(strength, out);
    }

    void collect_maxima(const cv::Mat& strength, std::vector<Detected>& out) {
        const double thr = threshold_;
        int ksize = std::min(15, std::max(3, track_radius_px_ * 2 + 1));
        if ((ksize & 1) == 0) ++ksize;  // ensure odd
        const cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(ksize, ksize));
        cv::Mat dil;
        cv::dilate(strength, dil, kernel);
        cv::Mat mask = (strength == dil) & (strength > thr);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                if (mask.at<uchar>(y, x)) {
                    out.push_back(Detected{static_cast<float>(x),
                                           static_cast<float>(y),
                                           strength.at<float>(y, x)});
                }
            }
        }
    }

    struct Track {
        int id{-1};
        float x{0.0F}, y{0.0F};
        float strength{0.0F};
        std::vector<cv::Point2f> traj;
        int len{0};
        int miss{0};
        bool updated{false};
    };

    void track(const std::vector<Detected>& detected) {
        for (auto& t : tracks_) t.updated = false;
        const float r = static_cast<float>(track_radius_px_);
        const float r2 = r * r;
        for (const auto& d : detected) {
            int best = -1;
            float best_d2 = r2;
            for (int k = 0; k < static_cast<int>(tracks_.size()); ++k) {
                if (!tracks_[k].updated) {
                    const float dx = tracks_[k].x - d.x;
                    const float dy = tracks_[k].y - d.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < best_d2) { best_d2 = d2; best = k; }
                }
            }
            if (best < 0) {
                Track t;
                t.id = next_track_id_++;
                t.x = d.x;
                t.y = d.y;
                t.strength = d.strength;
                t.traj.push_back(cv::Point2f(d.x, d.y));
                t.len = 1;
                t.updated = true;
                tracks_.push_back(t);
            } else {
                Track& t = tracks_[best];
                const float a = 0.4F;
                t.x = t.x * (1.0F - a) + d.x * a;
                t.y = t.y * (1.0F - a) + d.y * a;
                t.strength = d.strength;
                t.traj.push_back(cv::Point2f(t.x, t.y));
                if (t.traj.size() > 200) t.traj.erase(t.traj.begin());
                ++t.len;
                t.updated = true;
                t.miss = 0;
            }
        }
        // Age and prune unmatched tracks.
        std::vector<Track> kept;
        kept.reserve(tracks_.size());
        for (auto& t : tracks_) {
            if (!t.updated) ++t.miss;
            if (t.miss <= 3) kept.push_back(std::move(t));
        }
        tracks_.swap(kept);
    }

    void emit_corners() {
        corners_.clear();
        for (const auto& t : tracks_) {
            if (t.len < min_track_len_) continue;
            Corner c;
            c.x = t.x;
            c.y = t.y;
            c.strength = t.strength;
            c.track_id = t.id;
            c.trajectory = t.traj;
            corners_.push_back(c);
        }
    }

    int width_;
    int height_;
    Mode mode_;
    double accumulation_ms_{10.0};
    double threshold_{0.1};
    int track_radius_px_{5};
    int min_track_len_{10};
    int output_hz_{100};

    cv::Mat accum_;
    cv::Mat on_;
    cv::Mat off_;
    std::vector<Track> tracks_;
    std::vector<Corner> corners_;
    Metavision::timestamp last_event_t_{0};
    Metavision::timestamp last_frame_t_{0};
    Metavision::timestamp last_emit_t_{0};
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_CORNER_DETECTOR_H
