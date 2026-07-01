// algo/cv/object_tracker.h — event-level multi-object tracking.
//
// Self-developed (design §4.3.11), inspired by jAER RectangularClusterTracker
// et al. Four tracking modes:
//   RCT             — rectangular cluster tracker (centroid/mean update).
//   Median          — median position update (robust to outliers).
//   Kalman          — per-cluster KalmanFilter2D (position + velocity).
//   MultiHypothesis — multi-hypothesis centroid tree (occlusion handling).
// Each cluster implements ClusterInterface (position, velocity, bbox,
// trajectory, mass, age). Output: vector<TrackedObject>. Header-only.

#ifndef GUI_ALGO_CV_OBJECT_TRACKER_H
#define GUI_ALGO_CV_OBJECT_TRACKER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"
#include "algo/common/kalman_filter.h"
#include "algo/cv/cluster_interface.h"
#include "algo/cv/cluster_path_point.h"

namespace gui_algo {

/// @brief A tracked object snapshot emitted by the tracker.
struct TrackedObject {
    int id{-1};
    float x{0.0F};
    float y{0.0F};
    float vx{0.0F};
    float vy{0.0F};
    cv::Rect bbox;
    std::vector<ClusterPathPoint> trajectory;
    float age{0.0F};      ///< seconds since birth
    bool visible{false};
};

/// @brief Multi-mode event-level object tracker.
class ObjectTracker {
public:
    enum class Mode { RCT, Median, Kalman, MultiHypothesis };

    ObjectTracker(int width, int height, Mode mode = Mode::RCT)
        : width_(width), height_(height), mode_(mode) {}

    // Parameters (defaults per design §4.3.11) ----------------------------
    void set_mode(Mode m) { mode_ = m; }
    void set_cluster_size_px(int v) { cluster_size_px_ = clamp_i(v, 3, 50); }
    void set_cluster_time_us(int v) { cluster_time_us_ = clamp_i(v, 1000, 50000); }
    void set_min_cluster_events(int v) { min_cluster_events_ = clamp_i(v, 10, 500); }
    void set_max_lost_age_s(double v) { max_lost_age_s_ = clamp_d(v, 0.1, 5.0); }
    void set_enable_velocity_prediction(bool v) { enable_velocity_prediction_ = v; }

    Mode mode() const { return mode_; }
    int cluster_size_px() const { return cluster_size_px_; }
    int cluster_time_us() const { return cluster_time_us_; }
    int min_cluster_events() const { return min_cluster_events_; }
    double max_lost_age_s() const { return max_lost_age_s_; }
    bool enable_velocity_prediction() const { return enable_velocity_prediction_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Processes a batch of events, updating clusters.
    void process(const Event* events, std::size_t count) {
        Metavision::timestamp last_t = prev_batch_t_;
        prune_lost();
        for (std::size_t i = 0; i < count; ++i) {
            const Event& e = events[i];
            if (e.x >= width_ || e.y >= height_) continue;
            if (e.t > last_t) last_t = e.t;
            int best = -1;
            float best_d = static_cast<float>(cluster_size_px_);
            for (int k = 0; k < static_cast<int>(clusters_.size()); ++k) {
                if (e.t - clusters_[k].last_t() > cluster_time_us_) continue;
                const float d = clusters_[k].distance(e);
                if (d < best_d) { best_d = d; best = k; }
            }
            if (best < 0) {
                if (static_cast<int>(clusters_.size()) < kMaxClusters) {
                    clusters_.emplace_back(next_id_++, e, mode_, cluster_size_px_,
                                           cluster_time_us_,
                                           enable_velocity_prediction_,
                                           max_lost_us());
                }
            } else {
                clusters_[best].update(e);
            }
        }
        const Metavision::timestamp dt = last_t - prev_batch_t_;
        if (dt > 0) {
            for (auto& c : clusters_) c.age(dt);
        }
        prev_batch_t_ = last_t;
        merge_clusters();
        emit();
    }

    /// @brief Processes an event packet.
    void process(EventPacket& events) {
        process(events.data(), events.size());
    }

    /// @brief Returns the most recently emitted tracked objects.
    const std::vector<TrackedObject>& objects() const { return tracked_; }

    void reset() {
        clusters_.clear();
        tracked_.clear();
        next_id_ = 0;
        prev_batch_t_ = 0;
    }

private:
    static int clamp_i(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double clamp_d(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    Metavision::timestamp max_lost_us() const {
        return static_cast<Metavision::timestamp>(max_lost_age_s_ * 1e6);
    }

    static float median(std::vector<float>& v) {
        if (v.empty()) return 0.0F;
        const std::size_t n = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + static_cast<long>(n), v.end());
        return v[n];
    }

    /// @brief Concrete cluster implementing ClusterInterface.
    class Cluster : public ClusterInterface {
    public:
        struct Recent { float x, y; Metavision::timestamp t; };
        struct Hyp { float x, y; int mass; };

        Cluster(int id, const Event& seed, Mode mode, int cluster_size_px,
                int cluster_time_us, bool enable_velocity_prediction,
                Metavision::timestamp max_lost_us)
            : id_(id), mode_(mode), last_t_(seed.t),
              cluster_size_px_(cluster_size_px),
              cluster_time_us_(cluster_time_us),
              enable_velocity_prediction_(enable_velocity_prediction),
              max_lost_us_(max_lost_us),
              x_(static_cast<float>(seed.x)),
              y_(static_cast<float>(seed.y)),
              prev_x_(x_), prev_y_(y_), prev_pos_t_(seed.t),
              bbox_(static_cast<int>(seed.x), static_cast<int>(seed.y), 1, 1),
              radius_(static_cast<float>(cluster_size_px) * 0.5F) {
            recent_.push_back({x_, y_, seed.t});
            if (mode_ == Mode::Kalman) kf_.init(static_cast<double>(seed.x),
                                                static_cast<double>(seed.y));
            maybe_push_trajectory(seed);
        }

        // ClusterInterface implementation --------------------------------
        void update(const Event& e) override {
            const Metavision::timestamp prev_t = last_t_;
            since_last_us_ = 0;
            last_t_ = e.t;
            ++mass_;
            push_recent(e);
            switch (mode_) {
                case Mode::RCT: update_rct(); break;
                case Mode::Median: update_median(); break;
                case Mode::Kalman: update_kalman(e, prev_t); break;
                case Mode::MultiHypothesis: update_mh(e); break;
            }
            update_bbox(e);
            update_velocity(e);
            maybe_push_trajectory(e);
        }

        float distance(const Event& e) const override {
            const float dx = x_ - static_cast<float>(e.x);
            const float dy = y_ - static_cast<float>(e.y);
            return std::sqrt(dx * dx + dy * dy);
        }

        bool is_visible() const override {
            return since_last_us_ <= max_lost_us_ && mass_ >= 1;
        }

        void age(Metavision::timestamp dt_us) override {
            since_last_us_ += dt_us;
            age_us_ += dt_us;
            if (dt_us <= 0) return;
            if (mode_ == Mode::Kalman) {
                kf_.set_dt(static_cast<double>(dt_us) * 1e-6);
                kf_.predict();
                x_ = static_cast<float>(kf_.x());
                y_ = static_cast<float>(kf_.y());
                vx_ = static_cast<float>(kf_.vx());
                vy_ = static_cast<float>(kf_.vy());
            } else if (enable_velocity_prediction_) {
                const float dt_s = static_cast<float>(dt_us) * 1e-6F;
                x_ += vx_ * dt_s;
                y_ += vy_ * dt_s;
            }
        }

        float x() const override { return x_; }
        float y() const override { return y_; }
        float vx() const override { return vx_; }
        float vy() const override { return vy_; }
        cv::Rect bbox() const override { return bbox_; }
        const std::vector<ClusterPathPoint>& trajectory() const override {
            return trajectory_;
        }
        float mass() const override { return static_cast<float>(mass_); }
        Metavision::timestamp age_us() const override { return age_us_; }

        // Cluster-specific accessors ------------------------------------
        int id() const { return id_; }
        Metavision::timestamp last_t() const { return last_t_; }

        void absorb(Cluster& o) {
            const float m1 = static_cast<float>(mass_);
            const float m2 = static_cast<float>(o.mass_);
            const float tot = m1 + m2;
            if (tot > 0.0F) {
                x_ = (x_ * m1 + o.x_ * m2) / tot;
                y_ = (y_ * m1 + o.y_ * m2) / tot;
            }
            mass_ += o.mass_;
            bbox_ = bbox_ | o.bbox_;
            if (o.last_t_ > last_t_) last_t_ = o.last_t_;
        }

    private:
        void push_recent(const Event& e) {
            recent_.push_back({static_cast<float>(e.x),
                               static_cast<float>(e.y), e.t});
            while (!recent_.empty() && (e.t - recent_.front().t) > cluster_time_us_) {
                recent_.pop_front();
            }
            if (recent_.size() > 64) recent_.pop_front();
        }

        void update_rct() {
            double sx = 0.0, sy = 0.0;
            for (const auto& p : recent_) { sx += p.x; sy += p.y; }
            if (!recent_.empty()) {
                x_ = static_cast<float>(sx / static_cast<double>(recent_.size()));
                y_ = static_cast<float>(sy / static_cast<double>(recent_.size()));
            }
        }

        void update_median() {
            std::vector<float> xs, ys;
            xs.reserve(recent_.size());
            ys.reserve(recent_.size());
            for (const auto& p : recent_) { xs.push_back(p.x); ys.push_back(p.y); }
            x_ = median(xs);
            y_ = median(ys);
        }

        void update_kalman(const Event& e, Metavision::timestamp prev_t) {
            if (!kf_.initialized()) {
                kf_.init(static_cast<double>(e.x), static_cast<double>(e.y));
                x_ = static_cast<float>(e.x);
                y_ = static_cast<float>(e.y);
                return;
            }
            if (e.t > prev_t) {
                kf_.set_dt(static_cast<double>(e.t - prev_t) * 1e-6);
            }
            kf_.predict();
            kf_.update(static_cast<double>(e.x), static_cast<double>(e.y));
            x_ = static_cast<float>(kf_.x());
            y_ = static_cast<float>(kf_.y());
        }

        void update_mh(const Event& e) {
            const float ex = static_cast<float>(e.x);
            const float ey = static_cast<float>(e.y);
            const float bd = static_cast<float>(cluster_size_px_) *
                             static_cast<float>(cluster_size_px_);
            int best = -1;
            float best_d2 = bd;
            for (int k = 0; k < static_cast<int>(hyps_.size()); ++k) {
                const float dx = hyps_[k].x - ex;
                const float dy = hyps_[k].y - ey;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best_d2) { best_d2 = d2; best = k; }
            }
            if (best < 0) {
                if (static_cast<int>(hyps_.size()) >= 3) {
                    int lo = 0;
                    for (int k = 1; k < static_cast<int>(hyps_.size()); ++k) {
                        if (hyps_[k].mass < hyps_[lo].mass) lo = k;
                    }
                    hyps_[lo] = {ex, ey, 1};
                } else {
                    hyps_.push_back({ex, ey, 1});
                }
            } else {
                const float a = 0.3F;
                hyps_[best].x = hyps_[best].x * (1.0F - a) + ex * a;
                hyps_[best].y = hyps_[best].y * (1.0F - a) + ey * a;
                ++hyps_[best].mass;
            }
            if (!hyps_.empty()) {
                int mx = 0;
                for (int k = 1; k < static_cast<int>(hyps_.size()); ++k) {
                    if (hyps_[k].mass > hyps_[mx].mass) mx = k;
                }
                x_ = hyps_[mx].x;
                y_ = hyps_[mx].y;
            }
        }

        void update_bbox(const Event& e) {
            const cv::Rect r(static_cast<int>(e.x), static_cast<int>(e.y), 1, 1);
            bbox_ = bbox_ | r;
        }

        void update_velocity(const Event& e) {
            if (mode_ == Mode::Kalman) {
                vx_ = static_cast<float>(kf_.vx());
                vy_ = static_cast<float>(kf_.vy());
                return;
            }
            if (prev_pos_t_ > 0 && e.t > prev_pos_t_) {
                const float dt_s =
                    static_cast<float>(e.t - prev_pos_t_) * 1e-6F;
                if (dt_s > 0.0F) {
                    vx_ = (x_ - prev_x_) / dt_s;
                    vy_ = (y_ - prev_y_) / dt_s;
                }
            }
            prev_x_ = x_;
            prev_y_ = y_;
            prev_pos_t_ = e.t;
        }

        void maybe_push_trajectory(const Event& e) {
            if (trajectory_.empty() || e.t - last_traj_t_ >= cluster_time_us_) {
                trajectory_.push_back(ClusterPathPoint(x_, y_, vx_, vy_, e.t, radius_));
                if (trajectory_.size() > 500) trajectory_.erase(trajectory_.begin());
                last_traj_t_ = e.t;
            }
        }

        int id_;
        Mode mode_;
        Metavision::timestamp last_t_;
        Metavision::timestamp since_last_us_{0};
        Metavision::timestamp age_us_{0};
        Metavision::timestamp last_traj_t_{0};
        std::size_t mass_{1};
        int cluster_size_px_;
        int cluster_time_us_;
        bool enable_velocity_prediction_;
        Metavision::timestamp max_lost_us_;
        float x_, y_, vx_{0.0F}, vy_{0.0F};
        float prev_x_, prev_y_;
        Metavision::timestamp prev_pos_t_;
        cv::Rect bbox_;
        float radius_;
        std::deque<Recent> recent_;
        std::vector<Hyp> hyps_;
        std::vector<ClusterPathPoint> trajectory_;
        KalmanFilter2D kf_;
    };

    void prune_lost() {
        std::vector<Cluster> kept;
        kept.reserve(clusters_.size());
        for (auto& c : clusters_) {
            if (c.is_visible()) kept.push_back(std::move(c));
        }
        clusters_.swap(kept);
    }

    void merge_clusters() {
        if (clusters_.size() < 2) return;
        const float md = static_cast<float>(cluster_size_px_) * 0.5F;
        const float md2 = md * md;
        for (std::size_t i = 0; i < clusters_.size(); ++i) {
            for (std::size_t j = i + 1; j < clusters_.size();) {
                const float dx = clusters_[i].x() - clusters_[j].x();
                const float dy = clusters_[i].y() - clusters_[j].y();
                if (dx * dx + dy * dy <= md2) {
                    clusters_[i].absorb(clusters_[j]);
                    clusters_.erase(clusters_.begin() +
                                    static_cast<long>(j));
                } else {
                    ++j;
                }
            }
        }
    }

    void emit() {
        tracked_.clear();
        for (const auto& c : clusters_) {
            if (static_cast<int>(c.mass()) < min_cluster_events_) continue;
            if (!c.is_visible()) continue;
            TrackedObject o;
            o.id = c.id();
            o.x = c.x();
            o.y = c.y();
            o.vx = c.vx();
            o.vy = c.vy();
            o.bbox = c.bbox();
            o.trajectory = c.trajectory();
            o.age = static_cast<float>(c.age_us()) * 1e-6F;
            o.visible = true;
            tracked_.push_back(o);
        }
    }

    static constexpr int kMaxClusters = 2000;

    int width_;
    int height_;
    Mode mode_;
    int cluster_size_px_{10};
    int cluster_time_us_{5000};
    int min_cluster_events_{50};
    double max_lost_age_s_{1.0};
    bool enable_velocity_prediction_{true};

    std::vector<Cluster> clusters_;
    std::vector<TrackedObject> tracked_;
    int next_id_{0};
    Metavision::timestamp prev_batch_t_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_OBJECT_TRACKER_H
