// algo/analytics/event_to_video.h — Event -> grayscale image reconstruction.
//
// Design §4.4.2. Reconstructs grayscale frames from pure event streams via
// three paths: BardowVariational (default, TV-L1 variational with Chambolle-Pock
// primal-dual solver), InteractingMaps (iterative relaxation), and E2VID (DL,
// optional — full pipeline ported from rpg_e2vid with ONNX Runtime backend
// and heuristic fallback). Inspired by the referenced papers
// (Bardow et al. 2016; Cook et al. 2011) and the rpg_e2vid project. Header-only.

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
        InteractingMaps,    ///< Iterative relaxation.
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
    /// brightness (each event contributes +/- theta).
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
        p1_.clear();
        p2_.clear();
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

    /// @brief BardowVariational reconstruction: TV-L1 denoising of the log
    ///        intensity via Chambolle's projection algorithm.
    /// Solves: min_u ||u - f||^2/2 + lambda1 * TV(u), with f = log_intensity_.
    cv::Mat reconstruct_bardow() {
        const std::size_t N = static_cast<std::size_t>(width_) * height_;
        const std::vector<double>& f = log_intensity_;
        std::vector<double> u(N);
        if (lambda1_ <= 1e-9) {
            u = f;
            return to_gray(u);
        }
        // Chambolle projection: u = f - lambda * div(p).
        if (p1_.size() != N) {
            p1_.assign(N, 0.0);
            p2_.assign(N, 0.0);
        }
        const double tau = 1.0 / 16.0;  // <= 1/8 (Lipschitz constant of grad)
        const double inv_lambda = 1.0 / static_cast<double>(lambda1_);
        std::vector<double> phi(N);
        for (int iter = 0; iter < num_iterations_; ++iter) {
            // 1) phi = div(p) - f/lambda.
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const double div_p =
                        p1_[idx] - (x > 0 ? p1_[idx - 1] : 0.0) +
                        p2_[idx] - (y > 0 ? p2_[idx - width_] : 0.0);
                    phi[idx] = div_p - f[idx] * inv_lambda;
                }
            }
            // 2) p = (p + tau * grad(phi)) / (1 + tau * |grad(phi)|).
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    const double gx = (x + 1 < width_) ? phi[idx + 1] - phi[idx] : 0.0;
                    const double gy = (y + 1 < height_) ? phi[idx + width_] - phi[idx] : 0.0;
                    const double denom = 1.0 + tau * std::sqrt(gx * gx + gy * gy);
                    p1_[idx] = (p1_[idx] + tau * gx) / denom;
                    p2_[idx] = (p2_[idx] + tau * gy) / denom;
                }
            }
        }
        // 3) u = f - lambda * div(p).
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * width_ + x;
                const double div_p =
                    p1_[idx] - (x > 0 ? p1_[idx - 1] : 0.0) +
                    p2_[idx] - (y > 0 ? p2_[idx - width_] : 0.0);
                u[idx] = f[idx] - static_cast<double>(lambda1_) * div_p;
            }
        }
        return to_gray(u);
    }

    /// @brief InteractingMaps reconstruction: iterative Laplacian relaxation
    ///        of the log-intensity estimate toward a smooth, edge-preserving
    ///        solution (simplified version of the alternating-map scheme).
    cv::Mat reconstruct_interacting() {
        std::vector<double> u = log_intensity_;
        const double step = static_cast<double>(relaxation_step_);
        std::vector<double> next(u.size());
        for (int iter = 0; iter < im_iterations_; ++iter) {
            next = u;
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * width_ + x;
                    double sum = 0.0;
                    int cnt = 0;
                    if (x > 0) { sum += u[idx - 1]; ++cnt; }
                    if (x + 1 < width_) { sum += u[idx + 1]; ++cnt; }
                    if (y > 0) { sum += u[idx - width_]; ++cnt; }
                    if (y + 1 < height_) { sum += u[idx + width_]; ++cnt; }
                    if (cnt > 0) {
                        const double avg = sum / static_cast<double>(cnt);
                        next[idx] = u[idx] + step * (avg - u[idx]);
                    }
                }
            }
            u.swap(next);
        }
        return to_gray(u);
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

    // Base reconstruction state (used by non-E2VID modes).
    std::vector<double> log_intensity_;
    std::vector<double> p1_;
    std::vector<double> p2_;
    Metavision::timestamp current_t_{0};
    Metavision::timestamp last_frame_t_{0};   ///< Last get_frame() timestamp
    /// Exponential decay time constant for log_intensity_ (ms). Prevents
    /// unbounded accumulation which would freeze the normalized output.
    float decay_tau_ms_{50.0f};

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
