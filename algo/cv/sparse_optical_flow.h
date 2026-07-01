// algo/cv/sparse_optical_flow.h — sparse event-based optical flow estimation.
//
// Self-developed (design §4.3.9), inspired by the jAER rbodo optical-flow suite
// and ClusterBasedOpticalFlow. Four modes:
//   LocalPlanes  — local (x,y,t) plane fit (Benoit 2015); velocity = grad(T)/|grad(T)|².
//   LucasKanade  — time-surface gradient LK: solve S v = [sum Ix, sum Iy].
//   BlockMatch   — ABMOF-style block matching between consecutive accumulation
//                  frames on a downsampled grid.
//   ClusterOF    — centroid-cluster trajectory velocity (reuses §4.3.11 params).
// Output: vector<FlowVector> { x, y, vx, vy, confidence } (vx/vy in px/s).
// Header-only.

#ifndef GUI_ALGO_CV_SPARSE_OPTICAL_FLOW_H
#define GUI_ALGO_CV_SPARSE_OPTICAL_FLOW_H

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

/// @brief A sparse optical-flow vector (position + velocity + confidence).
struct FlowVector {
    float x{0.0F};
    float y{0.0F};
    float vx{0.0F};          ///< px/s
    float vy{0.0F};          ///< px/s
    float confidence{0.0F};  ///< [0,1]
    FlowVector() = default;
    FlowVector(float x_, float y_, float vx_, float vy_, float c_)
        : x(x_), y(y_), vx(vx_), vy(vy_), confidence(c_) {}
};

/// @brief Multi-mode sparse optical-flow estimator.
class SparseOpticalFlow {
public:
    enum class Mode { LocalPlanes, LucasKanade, BlockMatch, ClusterOF };

    SparseOpticalFlow(int width, int height, Mode mode = Mode::LocalPlanes)
        : width_(width), height_(height), mode_(mode),
          sae_(static_cast<std::size_t>(width) * height, 0) {}

    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }

    // LocalPlanes + LucasKanade (shared time window) ----------------------
    void set_time_window_us(int v) { time_window_us_ = clamp_i(v, 1000, 100000); }
    void set_spatial_radius_px(int v) { spatial_radius_px_ = clamp_i(v, 3, 30); }
    void set_min_events_per_cluster(int v) {
        min_events_per_cluster_ = clamp_i(v, 3, 100);
    }
    // LucasKanade ----------------------------------------------------------
    void set_block_size(int v) { block_size_ = clamp_i(v, 4, 64); }
    void set_step(int v) { step_ = clamp_i(v, 1, 32); }
    // BlockMatch -----------------------------------------------------------
    void set_downsample_factor(int v) { downsample_factor_ = clamp_i(v, 1, 8); }
    void set_block_match_time_window_us(int v) {
        bm_time_window_us_ = clamp_i(v, 1000, 200000);
    }
    void set_search_radius_px(int v) { search_radius_px_ = clamp_i(v, 1, 16); }
    // ClusterOF ------------------------------------------------------------
    void set_min_track_len(int v) { min_track_len_ = clamp_i(v, 3, 100); }
    void set_cluster_size_px(int v) { cluster_size_px_ = clamp_i(v, 3, 50); }
    void set_cluster_time_us(int v) { cluster_time_us_ = clamp_i(v, 1000, 50000); }

    int time_window_us() const { return time_window_us_; }
    int spatial_radius_px() const { return spatial_radius_px_; }
    int min_events_per_cluster() const { return min_events_per_cluster_; }
    int block_size() const { return block_size_; }
    int step() const { return step_; }
    int downsample_factor() const { return downsample_factor_; }
    int block_match_time_window_us() const { return bm_time_window_us_; }
    int search_radius_px() const { return search_radius_px_; }
    int min_track_len() const { return min_track_len_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Estimates flow for a batch of events; appends to @p out.
    void process(const Event* events, std::size_t count,
                 std::vector<FlowVector>& out) {
        switch (mode_) {
            case Mode::LocalPlanes: run_local_planes(events, count, out); break;
            case Mode::LucasKanade: run_lucas_kanade(events, count, out); break;
            case Mode::BlockMatch:  run_block_match(events, count, out); break;
            case Mode::ClusterOF:   run_cluster_of(events, count, out); break;
        }
    }

    /// @brief Convenience overload taking an event packet.
    void process(EventPacket& events, std::vector<FlowVector>& out) {
        process(events.data(), events.size(), out);
    }

    void reset() {
        std::fill(sae_.begin(), sae_.end(), 0);
        cur_.release();
        prev_.release();
        last_match_t_ = 0;
        of_clusters_.clear();
    }

private:
    static int clamp_i(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    std::size_t idx_of(int x, int y) const {
        return static_cast<std::size_t>(y) * width_ + x;
    }

    // --- LocalPlanes: plane fit t = a*x + b*y + c, v = grad/|grad|^2 -----
    void run_local_planes(const Event* events, std::size_t count,
                          std::vector<FlowVector>& out) {
        const Metavision::timestamp win = time_window_us_;
        const int r = spatial_radius_px_;
        for (std::size_t i = 0; i < count; ++i) {
            const Event& e = events[i];
            if (e.x >= width_ || e.y >= height_) continue;
            std::vector<double> xs, ys, ts;
            for (int dy = -r; dy <= r; ++dy) {
                const int ny = e.y + dy;
                if (ny < 0 || ny >= height_) continue;
                for (int dx = -r; dx <= r; ++dx) {
                    const int nx = e.x + dx;
                    if (nx < 0 || nx >= width_) continue;
                    const Metavision::timestamp lt = sae_[idx_of(nx, ny)];
                    if (lt == 0) continue;
                    const Metavision::timestamp diff = e.t - lt;
                    if (diff < 0 || diff > win) continue;
                    xs.push_back(static_cast<double>(nx));
                    ys.push_back(static_cast<double>(ny));
                    ts.push_back(static_cast<double>(lt));
                }
            }
            if (static_cast<int>(xs.size()) < min_events_per_cluster_) {
                sae_[idx_of(e.x, e.y)] = e.t;
                continue;
            }
            double a = 0.0, b = 0.0, c = 0.0;
            if (!fit_plane(xs, ys, ts, a, b, c)) {
                sae_[idx_of(e.x, e.y)] = e.t;
                continue;
            }
            const double denom = a * a + b * b;
            if (denom_small(denom)) {
                sae_[idx_of(e.x, e.y)] = e.t;
                continue;
            }
            const double vx = (a / denom) * 1e6;   // px/s
            const double vy = (b / denom) * 1e6;
            const double conf = plane_confidence(xs, ys, ts, a, b, c, win);
            if (conf > 0.0) {
                out.push_back(FlowVector(static_cast<float>(e.x),
                                         static_cast<float>(e.y),
                                         static_cast<float>(vx),
                                         static_cast<float>(vy),
                                         static_cast<float>(conf)));
            }
            sae_[idx_of(e.x, e.y)] = e.t;
        }
    }

    static bool denom_small(double d) { return std::fabs(d) < 1e-12; }

    static double det3(const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    static bool fit_plane(const std::vector<double>& xs,
                          const std::vector<double>& ys,
                          const std::vector<double>& ts,
                          double& a, double& b, double& c) {
        const double n = static_cast<double>(xs.size());
        double sx = 0, sy = 0, st = 0, sxx = 0, syy = 0, sxy = 0, sxt = 0, syt = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            sx += xs[i]; sy += ys[i]; st += ts[i];
            sxx += xs[i] * xs[i]; syy += ys[i] * ys[i];
            sxy += xs[i] * ys[i];
            sxt += xs[i] * ts[i]; syt += ys[i] * ts[i];
        }
        double m[3][3] = {{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, n}};
        double r[3] = {sxt, syt, st};
        const double det = det3(m);
        if (std::fabs(det) < 1e-9) return false;
        double m0[3][3] = {{r[0], m[0][1], m[0][2]},
                           {r[1], m[1][1], m[1][2]},
                           {r[2], m[2][1], m[2][2]}};
        double m1[3][3] = {{m[0][0], r[0], m[0][2]},
                           {m[1][0], r[1], m[1][2]},
                           {m[2][0], r[2], m[2][2]}};
        double m2[3][3] = {{m[0][0], m[0][1], r[0]},
                           {m[1][0], m[1][1], r[1]},
                           {m[2][0], m[2][1], r[2]}};
        a = det3(m0) / det;
        b = det3(m1) / det;
        c = det3(m2) / det;
        return true;
    }

    static double plane_confidence(const std::vector<double>& xs,
                                   const std::vector<double>& ys,
                                   const std::vector<double>& ts,
                                   double a, double b, double c,
                                   Metavision::timestamp win) {
        double sse = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            const double pred = a * xs[i] + b * ys[i] + c;
            const double d = ts[i] - pred;
            sse += d * d;
        }
        const double rmse = std::sqrt(sse / static_cast<double>(xs.size()));
        const double conf = 1.0 - rmse / static_cast<double>(win);
        return conf < 0.0 ? 0.0 : (conf > 1.0 ? 1.0 : conf);
    }

    // --- LucasKanade: time-surface gradient LK ---------------------------
    void run_lucas_kanade(const Event* events, std::size_t count,
                          std::vector<FlowVector>& out) {
        const Metavision::timestamp win = time_window_us_;
        const int bs = block_size_;
        const int half = bs / 2;
        // Update SAE first, then sample on a grid.
        for (std::size_t i = 0; i < count; ++i) {
            const Event& e = events[i];
            if (e.x < width_ && e.y < height_) sae_[idx_of(e.x, e.y)] = e.t;
        }
        const Metavision::timestamp last_t = count > 0 ? events[count - 1].t : 0;
        for (int gy = half; gy < height_ - half; gy += step_) {
            for (int gx = half; gx < width_ - half; gx += step_) {
                lk_at(gx, gy, half, last_t, win, out);
            }
        }
    }

    void lk_at(int gx, int gy, int half, Metavision::timestamp t,
               Metavision::timestamp win, std::vector<FlowVector>& out) {
        double sxx = 0.0, syy = 0.0, sxy = 0.0, sx = 0.0, sy = 0.0;
        int n = 0;
        for (int dy = -half; dy <= half; ++dy) {
            for (int dx = -half; dx <= half; ++dx) {
                const int x = gx + dx, y = gy + dy;
                const Metavision::timestamp tcc = sae_[idx_of(x, y)];
                if (tcc == 0 || t - tcc < 0 || t - tcc > win) continue;
                const Metavision::timestamp txp = (x + 1 < width_) ? sae_[idx_of(x + 1, y)] : 0;
                const Metavision::timestamp txm = (x - 1 >= 0)    ? sae_[idx_of(x - 1, y)] : 0;
                const Metavision::timestamp typ = (y + 1 < height_) ? sae_[idx_of(x, y + 1)] : 0;
                const Metavision::timestamp tym = (y - 1 >= 0)    ? sae_[idx_of(x, y - 1)] : 0;
                if (txp == 0 || txm == 0 || typ == 0 || tym == 0) continue;
                const double Ix = static_cast<double>(txp - txm) * 0.5;
                const double Iy = static_cast<double>(typ - tym) * 0.5;
                sxx += Ix * Ix; syy += Iy * Iy; sxy += Ix * Iy;
                sx += Ix; sy += Iy;
                ++n;
            }
        }
        if (n < 4) return;
        // Solve [[sxx,sxy],[sxy,syy]] [vx,vy] = [sx,sy]; constraint Ix*vx+Iy*vy=1.
        const double det = sxx * syy - sxy * sxy;
        if (std::fabs(det) < 1e-9) return;
        const double vx = (syy * sx - sxy * sy) / det * 1e6;   // px/s
        const double vy = (sxx * sy - sxy * sx) / det * 1e6;
        const double tr = sxx + syy;
        const double disc = std::sqrt(std::max(0.0, tr * tr - 4.0 * det));
        const double eig_min = 0.5 * (tr - disc);
        const double eig_max = 0.5 * (tr + disc);
        const double conf = eig_max > 0.0 ? eig_min / eig_max : 0.0;
        out.push_back(FlowVector(static_cast<float>(gx), static_cast<float>(gy),
                                 static_cast<float>(vx), static_cast<float>(vy),
                                 static_cast<float>(conf)));
    }

    // --- BlockMatch: ABMOF-style block matching on downsampled frames ----
    void run_block_match(const Event* events, std::size_t count,
                         std::vector<FlowVector>& out) {
        ensure_bm_mats();
        const int ds = downsample_factor_;
        for (std::size_t i = 0; i < count; ++i) {
            const Event& e = events[i];
            const int dx = e.x / ds;
            const int dy = e.y / ds;
            if (dx >= 0 && dx < bm_w_ && dy >= 0 && dy < bm_h_) {
                cur_.at<float>(dy, dx) += 1.0F;
                if (e.t > last_match_t_) last_match_t_ = e.t;
            }
        }
        if (last_match_t_ - last_emit_t_ < bm_time_window_us_) return;
        if (prev_.empty()) {
            prev_ = cur_.clone();
            cur_.setTo(0.0F);
            last_emit_t_ = last_match_t_;
            return;
        }
        const int sr_ds = std::max(1, search_radius_px_ / ds);
        const int mb = 8;       // match block (ds-px)
        const int mstep = 4;    // match stride (ds-px)
        for (int by = mb / 2; by < bm_h_ - mb / 2; by += mstep) {
            for (int bx = mb / 2; bx < bm_w_ - mb / 2; bx += mstep) {
                float best_dx = 0.0F, best_dy = 0.0F;
                double best_sad = -1.0;
                for (int sdy = -sr_ds; sdy <= sr_ds; ++sdy) {
                    for (int sdx = -sr_ds; sdx <= sr_ds; ++sdx) {
                        const double sad = block_sad(bx, by, sdx, sdy, mb);
                        if (best_sad < 0.0 || sad < best_sad) {
                            best_sad = sad;
                            best_dx = static_cast<float>(sdx);
                            best_dy = static_cast<float>(sdy);
                        }
                    }
                }
                const double vx = static_cast<double>(best_dx) * ds /
                                  (static_cast<double>(bm_time_window_us_) * 1e-6);
                const double vy = static_cast<double>(best_dy) * ds /
                                  (static_cast<double>(bm_time_window_us_) * 1e-6);
                const double area = static_cast<double>(mb * mb);
                const double conf = best_sad >= 0.0
                    ? 1.0 - best_sad / (area + 1.0) : 0.0;
                out.push_back(FlowVector(static_cast<float>(bx * ds),
                                         static_cast<float>(by * ds),
                                         static_cast<float>(vx),
                                         static_cast<float>(vy),
                                         static_cast<float>(conf < 0.0 ? 0.0 : conf)));
            }
        }
        prev_ = cur_.clone();
        cur_.setTo(0.0F);
        last_emit_t_ = last_match_t_;
    }

    double block_sad(int bx, int by, int sdx, int sdy, int mb) const {
        const int half = mb / 2;
        double sad = 0.0;
        for (int dy = -half; dy <= half; ++dy) {
            for (int dx = -half; dx <= half; ++dx) {
                const int cx = bx + dx, cy = by + dy;
                const int px = cx + sdx, py = cy + sdy;
                if (px < 0 || px >= bm_w_ || py < 0 || py >= bm_h_) {
                    sad += static_cast<double>(cur_.at<float>(cy, cx));
                    continue;
                }
                const double d = static_cast<double>(cur_.at<float>(cy, cx)) -
                                 static_cast<double>(prev_.at<float>(py, px));
                sad += std::fabs(d);
            }
        }
        return sad;
    }

    void ensure_bm_mats() {
        const int ds = downsample_factor_;
        const int w = (width_ + ds - 1) / ds;
        const int h = (height_ + ds - 1) / ds;
        if (bm_w_ != w || bm_h_ != h || cur_.empty()) {
            bm_w_ = w; bm_h_ = h;
            cur_ = cv::Mat::zeros(h, w, CV_32FC1);
            prev_ = cv::Mat();
        }
    }

    // --- ClusterOF: centroid trajectory velocity -------------------------
    struct OFCluster {
        float cx{0.0F}, cy{0.0F};
        Metavision::timestamp last_t{0};
        float prev_cx{0.0F}, prev_cy{0.0F};
        Metavision::timestamp prev_t{0};
        int mass{0};
        int history_len{0};
    };

    void run_cluster_of(const Event* events, std::size_t count,
                        std::vector<FlowVector>& out) {
        const float cs = static_cast<float>(cluster_size_px_);
        for (std::size_t i = 0; i < count; ++i) {
            const Event& e = events[i];
            int best = -1;
            float best_d2 = cs * cs;
            for (int k = 0; k < static_cast<int>(of_clusters_.size()); ++k) {
                const OFCluster& cl = of_clusters_[k];
                if (e.t - cl.last_t > cluster_time_us_) continue;
                const float ddx = cl.cx - static_cast<float>(e.x);
                const float ddy = cl.cy - static_cast<float>(e.y);
                const float d2 = ddx * ddx + ddy * ddy;
                if (d2 < best_d2) { best_d2 = d2; best = k; }
            }
            if (best < 0) {
                OFCluster cl;
                cl.cx = static_cast<float>(e.x);
                cl.cy = static_cast<float>(e.y);
                cl.last_t = e.t;
                cl.mass = 1;
                cl.history_len = 1;
                of_clusters_.push_back(cl);
            } else {
                OFCluster& cl = of_clusters_[best];
                // Record previous position before EMA update.
                if (cl.history_len == 0 ||
                    e.t - cl.prev_t >= cluster_time_us_) {
                    cl.prev_cx = cl.cx;
                    cl.prev_cy = cl.cy;
                    cl.prev_t = cl.last_t;
                }
                const float a = 0.2F;  // EMA smoothing
                cl.cx = cl.cx * (1.0F - a) + static_cast<float>(e.x) * a;
                cl.cy = cl.cy * (1.0F - a) + static_cast<float>(e.y) * a;
                cl.last_t = e.t;
                ++cl.mass;
                ++cl.history_len;
                if (cl.history_len >= min_track_len_ && e.t > cl.prev_t) {
                    const double dt_s =
                        static_cast<double>(e.t - cl.prev_t) * 1e-6;
                    if (dt_s > 0.0) {
                        const double vx =
                            static_cast<double>(cl.cx - cl.prev_cx) / dt_s;
                        const double vy =
                            static_cast<double>(cl.cy - cl.prev_cy) / dt_s;
                        out.push_back(FlowVector(cl.cx, cl.cy,
                                                 static_cast<float>(vx),
                                                 static_cast<float>(vy), 1.0F));
                    }
                }
            }
        }
        // Prune stale clusters.
        const Metavision::timestamp last_t = count > 0 ? events[count - 1].t : 0;
        std::vector<OFCluster> kept;
        kept.reserve(of_clusters_.size());
        for (auto& cl : of_clusters_) {
            if (last_t - cl.last_t <= cluster_time_us_ * 4) kept.push_back(cl);
        }
        of_clusters_.swap(kept);
    }

    int width_;
    int height_;
    Mode mode_;

    // LocalPlanes + LucasKanade params ------------------------------------
    int time_window_us_{10000};
    int spatial_radius_px_{8};
    int min_events_per_cluster_{10};
    int block_size_{16};
    int step_{8};

    // BlockMatch params ---------------------------------------------------
    int downsample_factor_{2};
    int bm_time_window_us_{20000};
    int search_radius_px_{4};
    int bm_w_{0};
    int bm_h_{0};
    cv::Mat cur_;
    cv::Mat prev_;
    Metavision::timestamp last_match_t_{0};
    Metavision::timestamp last_emit_t_{0};

    // ClusterOF params (reuse §4.3.11 defaults) ---------------------------
    int min_track_len_{5};
    int cluster_size_px_{10};
    int cluster_time_us_{5000};
    std::vector<OFCluster> of_clusters_;

    // Shared time surface (Surface of Active Events) ----------------------
    std::vector<Metavision::timestamp> sae_;
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_SPARSE_OPTICAL_FLOW_H
