// algo/cv/orientation_cluster.h — Orientation consensus filtering / clustering.
//
// Implements design §4.3.17 (jAER OrientationCluster). Reuses the 4-orientation
// filter (0°/45°/90°/135°) from §4.3.7: each event is classified by a 3x3
// time-surface neighbourhood, and only events whose orientation confidence
// exceeds coherence_threshold (with enough recent neighbours) are kept.
// Kept events are then grouped into connected components of consistent
// orientation; components smaller than min_cluster_size are discarded as
// incoherent noise. Header-only.

#ifndef GUI_ALGO_CV_ORIENTATION_CLUSTER_H
#define GUI_ALGO_CV_ORIENTATION_CLUSTER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief An orientation-consensus cluster.
struct OrientationClusterResult {
    cv::Point2f centroid;
    int orientation{-1};  ///< 0=0°, 1=45°, 2=90°, 3=135°.
    int size{0};          ///< Number of events in the cluster.
    int track_id{-1};     ///< Persistent track id, -1 if untracked.
};

/// @brief Clusters events by consistent local orientation.
class OrientationCluster {
public:
    OrientationCluster(int width, int height,
                       int time_window_us = 10000,
                       int min_neighbors = 2,
                       int min_cluster_size = 20,
                       float coherence_threshold = 0.7f)
        : width_(width), height_(height),
          time_window_us_(time_window_us),
          min_neighbors_(min_neighbors),
          min_cluster_size_(min_cluster_size),
          coherence_threshold_(coherence_threshold),
          track_tol_px_(20.0f),
          last_t_(static_cast<std::size_t>(width) * height, -1),
          orient_grid_(static_cast<std::size_t>(width) * height, -1),
          label_grid_(static_cast<std::size_t>(width) * height, -1) {}

    /// @brief Processes an event packet and returns orientation-consensus clusters.
    std::vector<OrientationClusterResult> process(const EventPacket& packet) {
        std::vector<OrientationClusterResult> result;
        if (packet.empty()) return result;
        std::fill(orient_grid_.begin(), orient_grid_.end(), -1);
        std::fill(label_grid_.begin(), label_grid_.end(), -1);

        for (const Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            const std::size_t idx =
                static_cast<std::size_t>(e.y) * width_ + e.x;
            last_t_[idx] = e.t;
            int orient = -1;
            float conf = 0.0f;
            compute_orientation(e.x, e.y, e.t, orient, conf);
            if (orient < 0 || conf < coherence_threshold_) continue;
            if (!enough_neighbors(e.x, e.y, e.t)) continue;
            orient_grid_[idx] = orient;
        }

        // Connected components (8-connectivity) of consistent orientation.
        std::vector<cv::Point2i> stack;
        int label = 0;
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * width_ + x;
                if (orient_grid_[idx] < 0 || label_grid_[idx] >= 0) continue;
                const int o = orient_grid_[idx];
                stack.clear();
                stack.push_back(cv::Point2i(x, y));
                label_grid_[idx] = label;
                int cnt = 0;
                double sx = 0.0, sy = 0.0;
                while (!stack.empty()) {
                    const cv::Point2i p = stack.back();
                    stack.pop_back();
                    ++cnt;
                    sx += p.x;
                    sy += p.y;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const int nx = p.x + dx;
                            const int ny = p.y + dy;
                            if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                            const std::size_t nidx =
                                static_cast<std::size_t>(ny) * width_ + nx;
                            if (orient_grid_[nidx] == o && label_grid_[nidx] < 0) {
                                label_grid_[nidx] = label;
                                stack.push_back(cv::Point2i(nx, ny));
                            }
                        }
                    }
                }
                if (cnt >= min_cluster_size_) {
                    OrientationClusterResult r;
                    r.centroid = cv::Point2f(static_cast<float>(sx / cnt),
                                             static_cast<float>(sy / cnt));
                    r.orientation = o;
                    r.size = cnt;
                    r.track_id = associate(r);
                    result.push_back(r);
                }
                ++label;
            }
        }
        return result;
    }

    // Parameter accessors ---------------------------------------------------
    int time_window_us() const { return time_window_us_; }
    int min_neighbors() const { return min_neighbors_; }
    int min_cluster_size() const { return min_cluster_size_; }
    float coherence_threshold() const { return coherence_threshold_; }
    void set_time_window_us(int v) { time_window_us_ = v; }
    void set_min_neighbors(int v) { min_neighbors_ = v; }
    void set_min_cluster_size(int v) { min_cluster_size_ = v; }
    void set_coherence_threshold(float v) { coherence_threshold_ = v; }

    void reset() {
        std::fill(last_t_.begin(), last_t_.end(),
                  static_cast<Metavision::timestamp>(-1));
        tracks_.clear();
        next_track_id_ = 0;
    }

private:
    struct Track {
        int id{-1};
        cv::Point2f last_centroid;
    };

    void compute_orientation(std::uint16_t x, std::uint16_t y,
                             Metavision::timestamp t,
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
            if (t - ta > time_window_us_ || t - tb > time_window_us_) continue;
            const Metavision::timestamp diff = ta > tb ? ta - tb : tb - ta;
            if (!found || diff < best_diff) {
                best_diff = diff;
                orient = o;
                found = true;
            }
        }
        if (!found) return;
        const float norm = static_cast<float>(best_diff) /
                           static_cast<float>(time_window_us_);
        conf = norm >= 1.0f ? 0.0f : 1.0f - norm;
    }

    bool enough_neighbors(std::uint16_t x, std::uint16_t y,
                          Metavision::timestamp t) const {
        const int px = static_cast<int>(x);
        const int py = static_cast<int>(y);
        int cnt = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = px + dx;
                const int ny = py + dy;
                if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                const Metavision::timestamp nt =
                    last_t_[static_cast<std::size_t>(ny) * width_ + nx];
                if (nt >= 0 && t - nt <= time_window_us_) ++cnt;
            }
        }
        return cnt >= min_neighbors_;
    }

    int associate(const OrientationClusterResult& r) {
        const float tol2 = track_tol_px_ * track_tol_px_;
        int best_id = -1;
        float best_d2 = tol2;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const float dx = tracks_[i].last_centroid.x - r.centroid.x;
            const float dy = tracks_[i].last_centroid.y - r.centroid.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_id = tracks_[i].id; }
        }
        if (best_id < 0) {
            best_id = next_track_id_++;
            tracks_.push_back(Track{best_id, r.centroid});
        } else {
            for (auto& tr : tracks_) {
                if (tr.id == best_id) { tr.last_centroid = r.centroid; break; }
            }
        }
        return best_id;
    }

    int width_;
    int height_;
    int time_window_us_;
    int min_neighbors_;
    int min_cluster_size_;
    float coherence_threshold_;
    float track_tol_px_;
    std::vector<Metavision::timestamp> last_t_;
    std::vector<int> orient_grid_;
    std::vector<int> label_grid_;
    std::vector<Track> tracks_;
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_ORIENTATION_CLUSTER_H
