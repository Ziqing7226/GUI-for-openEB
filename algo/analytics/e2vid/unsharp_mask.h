// algo/analytics/e2vid/unsharp_mask.h — Unsharp mask + bilateral filter.
//
// Design §4.4.2 (E2VID postprocessing). Implements the postprocessing filters
// from rpg_e2vid (utils/inference_utils.py::UnsharpMaskFilter and ImageFilter):
//   1. Unsharp mask: out = (1 + amount) * img - amount * blur(img)
//      where blur is a Gaussian convolution (kernel size 5, user sigma).
//   2. Bilateral filter: edge-preserving smoothing with user sigma.
// Header-only; uses OpenCV for efficient Gaussian/bilateral operations.

#ifndef GUI_ALGO_ANALYTICS_E2VID_UNSHARP_MASK_H
#define GUI_ALGO_ANALYTICS_E2VID_UNSHARP_MASK_H

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace gui_algo {

/// @brief Unsharp mask filter for reconstructed images.
class UnsharpMaskFilter {
public:
    /// @brief Constructs the filter.
    /// @param amount Sharpening strength (0 = disabled, typical 0.3).
    /// @param sigma Gaussian blur sigma (typical 1.0).
    UnsharpMaskFilter(float amount = 0.3f, float sigma = 1.0f)
        : amount_(clamp_amount(amount)), sigma_(clamp_sigma(sigma)) {}

    /// @brief Applies the unsharp mask to a CV_32FC1 image.
    cv::Mat operator()(const cv::Mat& img) const {
        if (img.empty() || amount_ <= 0.0f) return img;
        cv::Mat blurred;
        const int ksize = 5;  // fixed kernel size (matches rpg_e2vid)
        cv::GaussianBlur(img, blurred, cv::Size(ksize, ksize), sigma_, sigma_);
        cv::Mat sharp = (1.0f + amount_) * img - amount_ * blurred;
        return sharp;
    }

    void set_amount(float v) { amount_ = clamp_amount(v); }
    float amount() const { return amount_; }
    void set_sigma(float v) { sigma_ = clamp_sigma(v); }
    float sigma() const { return sigma_; }

private:
    static float clamp_amount(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 2.0f) return 2.0f;
        return v;
    }
    static float clamp_sigma(float v) {
        if (v < 0.1f) return 0.1f;
        if (v > 10.0f) return 10.0f;
        return v;
    }

    float amount_;
    float sigma_;
};

/// @brief Bilateral filter for edge-preserving denoising of reconstructed images.
class BilateralImageFilter {
public:
    /// @brief Constructs the filter.
    /// @param sigma Bilateral filter sigma (0 = disabled, typical 1.0).
    explicit BilateralImageFilter(float sigma = 0.0f)
        : sigma_(clamp_sigma(sigma)) {}

    /// @brief Applies the bilateral filter to a CV_8UC1 image.
    cv::Mat operator()(const cv::Mat& img) const {
        if (img.empty() || sigma_ <= 0.0f) return img;
        cv::Mat dst;
        cv::bilateralFilter(img, dst, 5,
                            25.0 * sigma_, 25.0 * sigma_);
        return dst;
    }

    void set_sigma(float v) { sigma_ = clamp_sigma(v); }
    float sigma() const { return sigma_; }

private:
    static float clamp_sigma(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 10.0f) return 10.0f;
        return v;
    }

    float sigma_;
};

} // namespace gui_algo

#endif // GUI_ALGO_ANALYTICS_E2VID_UNSHARP_MASK_H
