// algo/analytics/event_to_video.h — Event -> grayscale image reconstruction.
//
// Design §4.4.2. Reconstructs grayscale frames from pure event streams via
// three paths: BardowVariational (TV-L1 variational with joint optical-flow
// and intensity estimation via Chambolle-Pock primal-dual solver),
// InteractingMaps (six-map interconnection: I/G/V/F/C/R with rotation
// estimation), and E2VID (DL, default — full pipeline ported from rpg_e2vid
// with ONNX Runtime backend and heuristic fallback). Inspired by the
// referenced papers (Bardow et al. 2016; Cook et al. 2011) and the rpg_e2vid
// project. Header-only.

#ifndef GUI_ALGO_ANALYTICS_EVENT_TO_VIDEO_H
#define GUI_ALGO_ANALYTICS_EVENT_TO_VIDEO_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/analytics/e2vid/e2vid_inference.h"
#include "algo/analytics/e2vid/intensity_rescaler.h"
#include "algo/analytics/e2vid/unsharp_mask.h"

namespace gui_algo {

/// @brief Event-based grayscale image reconstruction (3 modes).
class EventToVideo {
public:
    enum class Mode {
        BardowVariational,  ///< TV-L1 variational (Chambolle-Pock), default.
        InteractingMaps,    ///< Six-map interconnection (Cook 2011).
        E2VID,              ///< Neural-network inference (ONNX Runtime or fallback).
    };

    /// @brief Constructs the reconstructor.
    /// @param width,height Sensor dimensions.
    /// @param mode Reconstruction mode.
    /// @param output_fps Output frame rate in Hz, [1, 120].
    EventToVideo(int width, int height,
                 Mode mode = Mode::BardowVariational,
                 int output_fps = 30)
        : width_(width), height_(height), mode_(mode),
          output_fps_(clamp_fps(output_fps)),
          log_intensity_(static_cast<std::size_t>(width) * height, 0.0),
          e2vid_(width, height) {}

    /// @brief Accumulates events.
    /// For BardowVariational / InteractingMaps: updates the per-pixel log
    /// brightness (each event contributes +/- theta). This serves as the
    /// event data term f (Bardow) or the temporal derivative map V (Cook).
    /// For E2VID: buffers events for the next voxel grid + inference call.
    void process(const Event* events, std::size_t n) {
        if (events == nullptr || n == 0) return;
        if (mode_ == Mode::E2VID) {
            // Buffer events for E2VID voxel grid inference.
            e2vid_event_buffer_.insert(e2vid_event_buffer_.end(),
                                       events, events + n);
            if (events[n - 1].t > current_t_) current_t_ = events[n - 1].t;
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                const Event& e = events[i];
                if (e.x >= width_ || e.y >= height_) continue;
                const std::size_t idx =
                    static_cast<std::size_t>(e.y) * width_ + e.x;
                log_intensity_[idx] += (e.is_on() ? theta_ : -theta_);
                if (e.t > current_t_) current_t_ = e.t;
            }
        }
    }

    /// @brief Reconstructs and returns the current grayscale frame (CV_8UC1).
    cv::Mat get_frame() {
        cv::Mat frame(height_, width_, CV_8UC1, cv::Scalar(0));
        if (width_ <= 0 || height_ <= 0) return frame;
        // Apply temporal decay so stale log-intensity values fade and the
        // reconstruction tracks recent events. Without this, log_intensity_
        // accumulates indefinitely (each event contributes +/-theta with no
        // forgetting factor) and the normalized to_gray() output freezes on
        // the residual pattern established by the first batch of events.
        if (current_t_ > last_frame_t_ && decay_tau_ms_ > 0.0f) {
            const double dt_us =
                static_cast<double>(current_t_ - last_frame_t_);
            const double tau_us =
                static_cast<double>(decay_tau_ms_) * 1000.0;
            const double decay = std::exp(-dt_us / tau_us);
            for (auto& v : log_intensity_) v *= decay;
        }
        last_frame_t_ = current_t_;
        switch (mode_) {
            case Mode::BardowVariational:
                frame = reconstruct_bardow();
                break;
            case Mode::InteractingMaps:
                frame = reconstruct_interacting();
                break;
            case Mode::E2VID:
                frame = reconstruct_e2vid();
                break;
        }
        return frame;
    }

    // Mode setters --------------------------------------------------------
    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }

    void set_output_fps(int fps) { output_fps_ = clamp_fps(fps); }
    int output_fps() const { return output_fps_; }

    // BardowVariational parameters ---------------------------------------
    void set_window_ms(float ms) { window_ms_ = clamp_window(ms); }
    float window_ms() const { return window_ms_; }

    void set_delta_t_ms(float ms) { delta_t_ms_ = clamp_delta_t(ms); }
    float delta_t_ms() const { return delta_t_ms_; }

    void set_theta(float t) { theta_ = clamp_theta(t); }
    float theta() const { return theta_; }

    void set_lambda1(float v) { lambda1_ = v; }
    void set_lambda2(float v) { lambda2_ = v; }
    void set_lambda3(float v) { lambda3_ = v; }
    void set_lambda4(float v) { lambda4_ = v; }
    void set_lambda5(float v) { lambda5_ = v; }
    void set_lambda6(float v) { lambda6_ = v; }

    void set_num_iterations(int n) { num_iterations_ = clamp_iter(n, 10, 500); }
    int num_iterations() const { return num_iterations_; }

    /// @brief Sets the log-intensity decay time constant in ms.
    /// Larger values = slower decay (longer memory); 0 disables decay.
    void set_decay_tau_ms(float ms) {
        decay_tau_ms_ = (ms < 0.0f) ? 0.0f : (ms > 1000.0f ? 1000.0f : ms);
    }
    float decay_tau_ms() const { return decay_tau_ms_; }

    // InteractingMaps parameters -----------------------------------------
    void set_relaxation_step(float s) {
        relaxation_step_ = (s > 0.0f && s < 1.0f) ? s : 0.1f;
    }
    float relaxation_step() const { return relaxation_step_; }

    void set_im_iterations(int n) { im_iterations_ = clamp_iter(n, 10, 1000); }
    int im_iterations() const { return im_iterations_; }

    /// @brief Sets the camera field-of-view (degrees) used to build the
    /// calibration map C for InteractingMaps. Default 60°.
    void set_fov_deg(float f) {
        fov_deg_ = (f < 10.0f) ? 10.0f : (f > 170.0f ? 170.0f : f);
        im_calib_dirty_ = true;  // force rebuild of C on next reconstruct
    }
    float fov_deg() const { return fov_deg_; }

    // E2VID parameters ----------------------------------------------------
    void set_model_path(const std::string& path) {
        model_path_ = path;
        e2vid_.load_model(path);
    }
    const std::string& model_path() const { return model_path_; }
    bool e2vid_model_loaded() const { return e2vid_.is_model_loaded(); }

    void set_e2vid_num_bins(int b) { e2vid_.set_num_bins(b); }
    int e2vid_num_bins() const { return e2vid_.num_bins(); }

    void set_e2vid_auto_hdr(bool v) { intensity_rescaler_.set_auto_hdr(v); }
    bool e2vid_auto_hdr() const { return intensity_rescaler_.auto_hdr(); }

    void set_e2vid_downsample(bool v) { e2vid_.set_downsample(v); }
    bool e2vid_downsample() const { return e2vid_.downsample(); }

    void set_unsharp_amount(float v) { unsharp_mask_.set_amount(v); }
    float unsharp_amount() const { return unsharp_mask_.amount(); }

    void set_unsharp_sigma(float v) { unsharp_mask_.set_sigma(v); }
    float unsharp_sigma() const { return unsharp_mask_.sigma(); }

    void set_bilateral_sigma(float v) { bilateral_filter_.set_sigma(v); }
    float bilateral_sigma() const { return bilateral_filter_.sigma(); }

    void set_e2vid_hot_pixel_mask(const std::vector<std::uint8_t>& mask) {
        e2vid_.set_hot_pixel_mask(mask);
    }
    void clear_e2vid_hot_pixel_mask() { e2vid_.clear_hot_pixel_mask(); }

    /// @brief Returns the minimum interval between output frames in us.
    Metavision::timestamp frame_interval_us() const {
        return static_cast<Metavision::timestamp>(1.0e6 / output_fps_);
    }

    /// @brief Resets the reconstruction state (log-intensity + E2VID pipeline).
    void reset() {
        std::fill(log_intensity_.begin(), log_intensity_.end(), 0.0);
        current_t_ = 0;
        last_frame_t_ = 0;
        e2vid_.reset();
        e2vid_event_buffer_.clear();
        intensity_rescaler_.reset();
        // Bardow state.
        std::fill(L_.begin(), L_.end(), 0.0);
        std::fill(L_prev_.begin(), L_prev_.end(), 0.0);
        std::fill(L_prior_.begin(), L_prior_.end(), 0.0);
        std::fill(L_tp_.begin(), L_tp_.end(), 0.0);
        std::fill(ux_.begin(), ux_.end(), 0.0);
        std::fill(uy_.begin(), uy_.end(), 0.0);
        std::fill(ux_prev_.begin(), ux_prev_.end(), 0.0);
        std::fill(uy_prev_.begin(), uy_prev_.end(), 0.0);
        std::fill(px_L_.begin(), px_L_.end(), 0.0);
        std::fill(py_L_.begin(), py_L_.end(), 0.0);
        std::fill(px_ux_.begin(), px_ux_.end(), 0.0);
        std::fill(py_ux_.begin(), py_ux_.end(), 0.0);
        std::fill(px_uy_.begin(), px_uy_.end(), 0.0);
        std::fill(py_uy_.begin(), py_uy_.end(), 0.0);
        // InteractingMaps state.
        std::fill(I_map_.begin(), I_map_.end(), 0.0);
        std::fill(Gx_.begin(), Gx_.end(), 0.0);
        std::fill(Gy_.begin(), Gy_.end(), 0.0);
        std::fill(Fx_.begin(), Fx_.end(), 0.0);
        std::fill(Fy_.begin(), Fy_.end(), 0.0);
        R_[0] = R_[1] = R_[2] = 0.0;
        im_calib_dirty_ = true;
    }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    static int clamp_fps(int fps) {
        if (fps < 1) return 1;
        if (fps > 120) return 120;
        return fps;
    }
    static float clamp_window(float ms) {
        if (ms < 10.0f) return 10.0f;
        if (ms > 500.0f) return 500.0f;
        return ms;
    }
    static float clamp_delta_t(float ms) {
        if (ms < 1.0f) return 1.0f;
        if (ms > 50.0f) return 50.0f;
        return ms;
    }
    static float clamp_theta(float t) {
        if (t < 0.05f) return 0.05f;
        if (t > 0.5f) return 0.5f;
        return t;
    }
    static int clamp_iter(int n, int lo, int hi) {
        if (n < lo) return lo;
        if (n > hi) return hi;
        return n;
    }

    /// @brief Converts a log-intensity buffer to a normalized CV_8UC1 frame.
    cv::Mat to_gray(const std::vector<double>& u) const {
        cv::Mat frame(height_, width_, CV_8UC1, cv::Scalar(0));
        double lo = 0.0, hi = 0.0;
        bool first = true;
        for (const double v : u) {
            if (first) { lo = v; hi = v; first = false; }
            else { if (v < lo) lo = v; if (v > hi) hi = v; }
        }
        const double range = hi - lo;
        for (int y = 0; y < height_; ++y) {
            std::uint8_t* row = frame.ptr<std::uint8_t>(y);
            for (int x = 0; x < width_; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * width_ + x;
                double g = range > 1e-9
                               ? (u[idx] - lo) / range
                               : 0.5;
                if (g < 0.0) g = 0.0;
                if (g > 1.0) g = 1.0;
                row[x] = static_cast<std::uint8_t>(g * 255.0 + 0.5);
            }
        }
        return frame;
    }

    /// @brief Chambolle projection TV denoising (scalar field).
    /// Solves: min_u ||u - g||^2/2 + lambda * TV(u)
    /// via Chambolle's semi-implicit projection algorithm.
    /// Dual variables px,py are maintained across calls for warm-start.
    void chambolle_tv(const std::vector<double>& g, double lambda,
                      int iters,
                      std::vector<double>& u,
                      std::vector<double>& px,
                      std::vector<double>& py) const {
        const std::size_t N = static_cast<std::size_t>(width_) * height_;
        if (u.size() != N) u.assign(N, 0.0);
        if (px.size() != N) px.assign(N, 0.0);
        if (py.size() != N) py.assign(N, 0.0);
        if (lambda <= 1e-9) { u = g; return; }
        const double tau = 1.0 / 16.0;  // <= 1/8 (Lipschitz constant of grad)
        const double inv_lambda = 1.0 / lambda;
        std::vector<double> phi(N);
        for (int iter = 0; iter < iters; ++iter) {
            // phi = div(p) - g/lambda.
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const double div_p =
                        px[idx] - (x > 0 ? px[idx - 1] : 0.0) +
                        py[idx] - (y > 0 ? py[idx - width_] : 0.0);
                    phi[idx] = div_p - g[idx] * inv_lambda;
                }
            }
            // p = (p + tau * grad(phi)) / (1 + tau * |grad(phi)|).
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const double gx = (x + 1 < width_) ? phi[idx + 1] - phi[idx] : 0.0;
                    const double gy = (y + 1 < height_) ? phi[idx + width_] - phi[idx] : 0.0;
                    const double denom = 1.0 + tau * std::sqrt(gx * gx + gy * gy);
                    px[idx] = (px[idx] + tau * gx) / denom;
                    py[idx] = (py[idx] + tau * gy) / denom;
                }
            }
        }
        // u = g - lambda * div(p).
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * width_ + x;
                const double div_p =
                    px[idx] - (x > 0 ? px[idx - 1] : 0.0) +
                    py[idx] - (y > 0 ? py[idx - width_] : 0.0);
                u[idx] = g[idx] - lambda * div_p;
            }
        }
    }

    /// @brief Solves a 3x3 linear system M·x = b via Cramer's rule.
    static void solve_3x3(const double M[9], const double b[3], double x[3]) {
        const double det = M[0]*(M[4]*M[8]-M[5]*M[7])
                         - M[1]*(M[3]*M[8]-M[5]*M[6])
                         + M[2]*(M[3]*M[7]-M[4]*M[6]);
        if (std::abs(det) < 1e-12) { x[0]=x[1]=x[2]=0.0; return; }
        const double inv = 1.0 / det;
        // Cramer's rule: replace each column with b.
        x[0] = (b[0]*(M[4]*M[8]-M[5]*M[7]) - M[1]*(b[1]*M[8]-M[5]*b[2])
               + M[2]*(b[1]*M[7]-M[4]*b[2])) * inv;
        x[1] = (M[0]*(b[1]*M[8]-M[5]*b[2]) - b[0]*(M[3]*M[8]-M[5]*M[6])
               + M[2]*(M[3]*b[2]-b[1]*M[6])) * inv;
        x[2] = (M[0]*(M[4]*b[2]-b[1]*M[7]) - M[1]*(M[3]*b[2]-b[1]*M[6])
               + b[0]*(M[3]*M[7]-M[4]*M[6])) * inv;
    }

    // =====================================================================
    // BardowVariational: joint optical-flow and intensity estimation.
    //
    // Full reproduction of Bardow et al. 2016 CVPR (Eq. 3), adapted to the
    // real-time 2D framework by approximating the spatio-temporal volume
    // (M x N x K) with a single-time-step sliding window (current frame vs
    // previous frame). All six regularization terms (lambda1..6) and the
    // joint estimation of velocity field u and log-intensity L are preserved:
    //   lambda1: TV(u)        — spatial smoothness of optical flow.
    //   lambda2: ||u - u_prev|| — temporal smoothness of flow.
    //   lambda3: TV(L)        — spatial smoothness of intensity.
    //   lambda4: |<grad L, dt*u> + (L - L_prev)| — optical-flow constraint
    //           (brightness constancy, first-order Taylor approximation).
    //   lambda5: h_theta(L - L_tp) — no-event dead-zone constraint.
    //   lambda6: ||L - L_prior||^2 — prior image retention.
    // The event data term (|L(t_i) - L(t_{i-1}) - theta*rho|) is represented
    // by the event-accumulated log_intensity_ serving as data fidelity target.
    // Optimization uses alternating Chambolle-Pock primal-dual updates.
    // =====================================================================
    cv::Mat reconstruct_bardow() {
        const std::size_t N = static_cast<std::size_t>(width_) * height_;
        const std::vector<double>& f = log_intensity_;  // event data term
        // Lazy initialization of state buffers.
        if (L_.size() != N) {
            L_.assign(N, 0.0); L_prev_.assign(N, 0.0);
            L_prior_.assign(N, 0.0); L_tp_.assign(N, 0.0);
            ux_.assign(N, 0.0); uy_.assign(N, 0.0);
            ux_prev_.assign(N, 0.0); uy_prev_.assign(N, 0.0);
        }
        const double dt = static_cast<double>(delta_t_ms_) * 1e-3;  // seconds
        // Initialize L from event data on first frame (warm start).
        if (im_first_frame_) {
            L_ = f; L_prev_ = f; L_prior_ = f; L_tp_ = f;
            im_first_frame_ = false;
        }
        // --- Alternating primal-dual optimization (Eq. 5, biconvex split) ---
        for (int iter = 0; iter < num_iterations_; ++iter) {
            // ===== L update (fix u) =====
            // Build TV-denoising target g combining event data (weight 1),
            // optical-flow temporal prediction (lambda4), and prior image
            // (lambda6).
            std::vector<double> g(N);
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    // Spatial gradient of previous L (for flow prediction).
                    const double gx = (x + 1 < width_)
                        ? L_prev_[idx + 1] - L_prev_[idx] : 0.0;
                    const double gy = (y + 1 < height_)
                        ? L_prev_[idx + width_] - L_prev_[idx] : 0.0;
                    // Optical-flow consistency: L_t = -<grad L, u> * dt,
                    // i.e. L should be L_prev + dt * <grad L, u>.
                    const double L_flow =
                        L_prev_[idx] + dt * (ux_[idx] * gx + uy_[idx] * gy);
                    g[idx] = (f[idx]
                              + static_cast<double>(lambda4_) * L_flow
                              + static_cast<double>(lambda6_) * L_prior_[idx])
                           / (1.0 + lambda4_ + lambda6_);
                }
            }
            // TV denoising with lambda3 (1 Chambolle iteration per outer iter;
            // num_iterations_ outer iterations provide full convergence).
            chambolle_tv(g, lambda3_, 1, L_, px_L_, py_L_);
            // No-event dead-zone (lambda5): soft-threshold |L - L_tp| around
            // theta (Eq. 4, 6). Within [-theta, theta] no cost; beyond, L1.
            if (lambda5_ > 1e-9) {
                const double shift = static_cast<double>(lambda5_);
                for (std::size_t i = 0; i < N; ++i) {
                    const double diff = L_[i] - L_tp_[i];
                    const double thr = theta_ + shift;
                    if (diff > thr)       L_[i] -= shift;
                    else if (diff < -thr) L_[i] += shift;
                }
            }
            // ===== u update (fix L) =====
            // Optical-flow constraint (Eq. 2): <grad L, u> = -L_t.
            // L_t = (L - L_prev) / dt. Minimum-norm solution (Horn-Schunck
            // style): u_target = -L_t / |grad L|^2 * grad L.
            std::vector<double> utx(N), uty(N);
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const double gx = (x + 1 < width_)
                        ? L_[idx + 1] - L_[idx] : 0.0;
                    const double gy = (y + 1 < height_)
                        ? L_[idx + width_] - L_[idx] : 0.0;
                    const double Lt = (L_[idx] - L_prev_[idx])
                                    / std::max(dt, 1e-9);
                    const double g2 = gx * gx + gy * gy + 1e-6;
                    utx[idx] = -Lt * gx / g2;
                    uty[idx] = -Lt * gy / g2;
                }
            }
            // Temporal smoothness (lambda2): blend with previous flow.
            if (lambda2_ > 1e-9) {
                const double w = lambda2_;
                for (std::size_t i = 0; i < N; ++i) {
                    utx[i] = (utx[i] + w * ux_prev_[i]) / (1.0 + w);
                    uty[i] = (uty[i] + w * uy_prev_[i]) / (1.0 + w);
                }
            }
            // TV denoising of flow components with lambda1.
            chambolle_tv(utx, lambda1_, 1, ux_, px_ux_, py_ux_);
            chambolle_tv(uty, lambda1_, 1, uy_, px_uy_, py_uy_);
        }
        // Persist state for next frame's sliding window.
        L_prior_ = L_;   // prior image L-hat
        L_prev_ = L_;    // previous-frame L
        ux_prev_ = ux_;  // previous-frame flow
        uy_prev_ = uy_;
        L_tp_ = L_;      // intensity at last event (approx: current frame)
        return to_gray(L_);
    }

    // =====================================================================
    // InteractingMaps: six-map interconnection (Cook et al. 2011 IJCNN).
    //
    // Full reproduction of the interacting-map network with six maps:
    //   I: light intensity          (W+1 x H+1, scalar)
    //   G: spatial gradient         (W x H, 2D vector)
    //   V: temporal derivative      (W x H, scalar) — input from events
    //   F: optical flow             (W x H, 2D vector)
    //   C: camera calibration       (W x H, 3D vector) — precomputed constant
    //   R: camera rotation          (single 3D vector)
    //
    // Three relations drive relaxation updates:
    //   (i)   G = grad(I)              — gradient definition (Eq. 2, 6, 7-9)
    //   (ii)  -V = F . G               — optical-flow constraint (Eq. 1, 5)
    //   (iii) F = m32(R x C)           — 3D rotation geometry (Eq. 3, 11-13)
    //
    // Each relation pulls a map toward the candidate satisfying it, using a
    // relaxation step. R is updated via linear least squares (Eq. 10, 13).
    // =====================================================================
    cv::Mat reconstruct_interacting() {
        const int W = width_, H = height_;
        const std::size_t N = static_cast<std::size_t>(W) * H;
        const std::size_t NI = static_cast<std::size_t>(W + 1) * (H + 1);
        const std::vector<double>& V = log_intensity_;  // input map V
        // Lazy initialization.
        if (I_map_.size() != NI) {
            I_map_.assign(NI, 0.0);
            Gx_.assign(N, 0.0); Gy_.assign(N, 0.0);
            Fx_.assign(N, 0.0); Fy_.assign(N, 0.0);
            R_[0] = R_[1] = R_[2] = 0.0;
            im_calib_dirty_ = true;
        }
        // Build calibration map C and precomputed projection matrix if needed.
        if (im_calib_dirty_ || Cx_.size() != N) {
            build_calibration_map();
            im_calib_dirty_ = false;
        }
        const double step = static_cast<double>(relaxation_step_);
        const int WI = W + 1;  // I map row stride
        for (int iter = 0; iter < im_iterations_; ++iter) {
            // (i) G = grad(I): update G toward grad(I) (Eq. 6).
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * W + x;
                    const std::size_t ii =
                        static_cast<std::size_t>(y) * WI + x;
                    const double gx = I_map_[ii + 1] - I_map_[ii];
                    const double gy = I_map_[ii + WI] - I_map_[ii];
                    Gx_[idx] = (1.0 - step) * Gx_[idx] + step * gx;
                    Gy_[idx] = (1.0 - step) * Gy_[idx] + step * gy;
                }
            }
            // (i) I from G (Eq. 7-9): Psi = G - grad(I); I -= step * Psi_hat.
            for (int y = 0; y <= H; ++y) {
                for (int x = 0; x <= W; ++x) {
                    const std::size_t ii =
                        static_cast<std::size_t>(y) * WI + x;
                    double psix = 0.0, psiy = 0.0;
                    if (x < W && y < H) {
                        const std::size_t idx =
                            static_cast<std::size_t>(y) * W + x;
                        const double gx = I_map_[ii + 1] - I_map_[ii];
                        const double gy = I_map_[ii + WI] - I_map_[ii];
                        psix = Gx_[idx] - gx;
                        psiy = Gy_[idx] - gy;
                    }
                    // Psi_hat_x = Psi(x,y) - Psi(x-1,y) (Eq. 8).
                    double phat_x = psix;
                    if (x > 0 && y < H) {
                        const std::size_t idx_p =
                            static_cast<std::size_t>(y) * W + (x - 1);
                        const std::size_t ii_p = ii - 1;
                        const double gx_p = I_map_[ii_p + 1] - I_map_[ii_p];
                        phat_x -= (Gx_[idx_p] - gx_p);
                    }
                    // Psi_hat_y = Psi(x,y) - Psi(x,y-1).
                    double phat_y = psiy;
                    if (y > 0 && x < W) {
                        const std::size_t idx_p =
                            static_cast<std::size_t>(y - 1) * W + x;
                        const std::size_t ii_p = ii - WI;
                        const double gy_p = I_map_[ii_p + WI] - I_map_[ii_p];
                        phat_y -= (Gy_[idx_p] - gy_p);
                    }
                    I_map_[ii] = (1.0 - step) * I_map_[ii]
                               + step * (I_map_[ii] - phat_x - phat_y);
                }
            }
            // (ii) F from V, G (Eq. 5): gradient descent on Q = (V + F.G)^2.
            for (std::size_t i = 0; i < N; ++i) {
                const double res = V[i] + Fx_[i] * Gx_[i] + Fy_[i] * Gy_[i];
                Fx_[i] -= step * 2.0 * Gx_[i] * res;
                Fy_[i] -= step * 2.0 * Gy_[i] * res;
            }
            // (ii) G from V, F (analogous): G -= step * 2 * F * (V + F.G).
            for (std::size_t i = 0; i < N; ++i) {
                const double res = V[i] + Fx_[i] * Gx_[i] + Fy_[i] * Gy_[i];
                Gx_[i] -= step * 2.0 * Fx_[i] * res;
                Gy_[i] -= step * 2.0 * Fy_[i] * res;
            }
            // (iii) F from R (Eq. 11): F = (1-d)F + d * m32(R x C).
            for (std::size_t i = 0; i < N; ++i) {
                // R x C (3D cross product).
                const double tx = R_[1] * Cz_[i] - R_[2] * Cy_[i];
                const double ty = R_[2] * Cx_[i] - R_[0] * Cz_[i];
                // m32: project 3D tangent to 2D image (divide by Cz).
                const double cz = Cz_[i] + 1e-9;
                Fx_[i] = (1.0 - step) * Fx_[i] + step * (tx / cz);
                Fy_[i] = (1.0 - step) * Fy_[i] + step * (ty / cz);
            }
            // (iii) R from F (Eq. 10, 13): linear least squares.
            // d^2 = |R - C x m23(F)|^2 - (R.C)^2  =>  (I - CC^T) R = b.
            // Precomputed M = sum(I - C_i C_i^T); b = sum(P_i (C_i x m23(F_i))).
            {
                double b[3] = {0.0, 0.0, 0.0};
                for (std::size_t i = 0; i < N; ++i) {
                    // m23(F) approx (Fx, Fy, 0) in image plane (Eq. 12).
                    // C x m23(F):
                    const double rx = Cy_[i] * 0.0 - Cz_[i] * Fy_[i];
                    const double ry = Cz_[i] * Fx_[i] - Cx_[i] * 0.0;
                    const double rz = Cx_[i] * Fy_[i] - Cy_[i] * Fx_[i];
                    // Apply precomputed projection P_i = I - C_i C_i^T to the
                    // candidate (rx,ry,rz) and accumulate into b.
                    // P * v = v - C (C.v).
                    const double cdot = Cx_[i] * rx + Cy_[i] * ry + Cz_[i] * rz;
                    b[0] += rx - Cx_[i] * cdot;
                    b[1] += ry - Cy_[i] * cdot;
                    b[2] += rz - Cz_[i] * cdot;
                }
                double Rnew[3];
                solve_3x3(im_Mat_, b, Rnew);
                R_[0] = (1.0 - step) * R_[0] + step * Rnew[0];
                R_[1] = (1.0 - step) * R_[1] + step * Rnew[1];
                R_[2] = (1.0 - step) * R_[2] + step * Rnew[2];
            }
        }
        // Output grayscale from I (sample first W x H of the (W+1)x(H+1) map).
        std::vector<double> out(N);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                out[static_cast<std::size_t>(y) * W + x] =
                    I_map_[static_cast<std::size_t>(y) * WI + x];
            }
        }
        return to_gray(out);
    }

    /// @brief Builds the camera calibration map C (3D unit direction per
    /// pixel) from the field-of-view, and precomputes the least-squares
    /// projection matrix M = sum(I - C_i C_i^T) used for rotation estimation.
    void build_calibration_map() {
        const int W = width_, H = height_;
        const std::size_t N = static_cast<std::size_t>(W) * H;
        Cx_.assign(N, 0.0); Cy_.assign(N, 0.0); Cz_.assign(N, 0.0);
        const double fov = static_cast<double>(fov_deg_) * M_PI / 180.0;
        const double f = (W > H ? W : H) / 2.0 / std::tan(fov / 2.0);
        const double cx0 = (W - 1) / 2.0;
        const double cy0 = (H - 1) / 2.0;
        // Accumulate M = sum(I - C C^T).
        double M[9] = {0};
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * W + x;
                double dx = (x - cx0) / f;
                double dy = (y - cy0) / f;
                double dz = 1.0;
                const double norm = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= norm; dy /= norm; dz /= norm;
                Cx_[idx] = dx; Cy_[idx] = dy; Cz_[idx] = dz;
                // M += I - C C^T.
                M[0] += 1.0 - dx * dx;
                M[1] += -dx * dy;
                M[2] += -dx * dz;
                M[3] += -dy * dx;
                M[4] += 1.0 - dy * dy;
                M[5] += -dy * dz;
                M[6] += -dz * dx;
                M[7] += -dz * dy;
                M[8] += 1.0 - dz * dz;
            }
        }
        for (int i = 0; i < 9; ++i) im_Mat_[i] = M[i];
    }

    /// @brief E2VID reconstruction: voxel grid -> neural network inference
    ///        (or heuristic fallback) -> unsharp mask -> intensity rescale ->
    ///        bilateral filter. Ported from rpg_e2vid image_reconstructor.py.
    /// Pipeline order matches the reference: pad events -> infer ->
    /// unsharp_mask -> intensity_rescale -> crop -> bilateral.
    cv::Mat reconstruct_e2vid() {
        if (e2vid_event_buffer_.empty()) {
            return cv::Mat::zeros(height_, width_, CV_8UC1);
        }
        // 1. Run E2VID inference.
        //    ONNX path returns CV_32FC1 padded image in [0,1] (crop_h x crop_w).
        //    Heuristic path returns CV_8UC1 sensor-sized image in [0,255].
        cv::Mat raw = e2vid_.infer(e2vid_event_buffer_.data(),
                                   e2vid_event_buffer_.size());
        // 2. Unsharp mask (skip if raw is 8-bit — heuristic already returns 8-bit).
        cv::Mat sharpened;
        if (raw.type() == CV_32FC1) {
            sharpened = unsharp_mask_(raw);
        } else {
            sharpened = raw;
        }
        // 3. Intensity rescale (if float) or use directly (if 8-bit).
        cv::Mat rescaled;
        if (sharpened.type() == CV_32FC1) {
            rescaled = intensity_rescaler_(sharpened);
        } else {
            rescaled = sharpened;
        }
        // 4. Crop padded image back to sensor size (no-op for heuristic path).
        cv::Mat cropped = e2vid_.crop_to_sensor(rescaled);
        // 5. Bilateral filter (edge-preserving denoising).
        cv::Mat filtered = bilateral_filter_(cropped);
        // Clear the event buffer for the next frame.
        e2vid_event_buffer_.clear();
        return filtered;
    }

    int width_;
    int height_;
    Mode mode_;
    int output_fps_;

    // BardowVariational parameters.
    float window_ms_{15.0f};
    float delta_t_ms_{15.0f};
    float theta_{0.22f};
    float lambda1_{0.02f};
    float lambda2_{0.05f};
    float lambda3_{0.02f};
    float lambda4_{0.2f};
    float lambda5_{0.1f};
    float lambda6_{1.0f};
    int num_iterations_{100};

    // InteractingMaps parameters.
    float relaxation_step_{0.1f};
    int im_iterations_{50};
    float fov_deg_{60.0f};

    // Base reconstruction state (used by non-E2VID modes).
    std::vector<double> log_intensity_;
    Metavision::timestamp current_t_{0};
    Metavision::timestamp last_frame_t_{0};   ///< Last get_frame() timestamp
    /// Exponential decay time constant for log_intensity_ (ms). Prevents
    /// unbounded accumulation which would freeze the normalized output.
    float decay_tau_ms_{50.0f};

    // --- BardowVariational optimization state ---
    std::vector<double> L_;         ///< Current log-intensity estimate.
    std::vector<double> L_prev_;    ///< Previous-frame L (temporal term).
    std::vector<double> L_prior_;   ///< Prior image L-hat (lambda6).
    std::vector<double> L_tp_;      ///< Intensity at last event (dead-zone).
    std::vector<double> ux_, uy_;   ///< Current optical-flow field.
    std::vector<double> ux_prev_, uy_prev_;  ///< Previous-frame flow.
    // Chambolle TV dual variables (warm-started across iterations).
    std::vector<double> px_L_, py_L_;    ///< Duals for L TV (lambda3).
    std::vector<double> px_ux_, py_ux_;  ///< Duals for ux TV (lambda1).
    std::vector<double> px_uy_, py_uy_;  ///< Duals for uy TV (lambda1).
    bool im_first_frame_{true};

    // --- InteractingMaps state ---
    std::vector<double> I_map_;  ///< Intensity map (W+1 x H+1).
    std::vector<double> Gx_, Gy_;  ///< Spatial gradient (W x H).
    std::vector<double> Fx_, Fy_;  ///< Optical flow (W x H).
    double R_[3] = {0.0, 0.0, 0.0};  ///< Global rotation vector.
    std::vector<double> Cx_, Cy_, Cz_;  ///< Calibration map C (W x H, 3D).
    double im_Mat_[9] = {0};  ///< Precomputed sum(I - C C^T) for R least-squares.
    bool im_calib_dirty_{true};

    // E2VID parameters.
    std::string model_path_;

    // E2VID pipeline components (ported from rpg_e2vid).
    E2VIDInference e2vid_;
    IntensityRescaler intensity_rescaler_;
    UnsharpMaskFilter unsharp_mask_;
    BilateralImageFilter bilateral_filter_;
    std::vector<Event> e2vid_event_buffer_;
};

} // namespace gui_algo

#endif // GUI_ALGO_ANALYTICS_EVENT_TO_VIDEO_H
