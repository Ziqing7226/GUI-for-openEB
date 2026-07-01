// algo/analytics/e2vid/e2vid_inference.h — E2VID neural-network inference.
//
// Design §4.4.2 (E2VID DL path). Wraps the E2VID / UNet-Recurrent model
// inference for event-based grayscale reconstruction. Ported from
// rpg_e2vid (image_reconstructor.py, model/unet.py).
//
// Architecture (rpg_e2vid):
//   - Input: 1 x num_bins x H x W event voxel grid (float32)
//   - Model: UNet or UNetRecurrent (ConvLSTM/ConvGRU) with skip connections,
//     4 encoders, 2 residual blocks, 4 decoders, sigmoid output
//   - Output: 1 x 1 x H x W grayscale image in [0, 1]
//
// Backends:
//   - ONNX Runtime (preferred): load exported .onnx model, run inference.
//     Conditionally compiled when ONNX Runtime is found via CMake.
//   - Heuristic fallback (always available): when no model is loaded,
//     reconstructs by summing voxel bins and applying sigmoid-like mapping.
//     This produces a crude but usable preview without the neural network.
//
// The CropParameters logic (padding to power-of-2 divisible sizes) is also
// implemented to match the original rpg_e2vid preprocessing. Header-only.

#ifndef GUI_ALGO_ANALYTICS_E2VID_E2VID_INFERENCE_H
#define GUI_ALGO_ANALYTICS_E2VID_E2VID_INFERENCE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "algo/common/event.h"
#include "algo/analytics/e2vid/event_voxel_grid.h"

// Conditional ONNX Runtime support.
#if defined(GUI_ALGO_HAS_ONNXRUNTIME)
#include <onnxruntime_cxx_api.h>
#endif

namespace gui_algo {

/// @brief Crop/padding parameters for UNet (matches rpg_e2vid CropParameters).
struct E2VIDCropParams {
    int width{0};
    int height{0};
    int crop_width{0};   ///< Padded width (divisible by 2^num_encoders).
    int crop_height{0};  ///< Padded height (divisible by 2^num_encoders).
    int pad_top{0};
    int pad_bottom{0};
    int pad_left{0};
    int pad_right{0};

    /// @brief Computes crop parameters for a given sensor size and UNet depth.
    static E2VIDCropParams compute(int width, int height, int num_encoders) {
        E2VIDCropParams p;
        p.width = width;
        p.height = height;
        const int factor = 1 << num_encoders;  // 2^num_encoders
        p.crop_width = optimal_crop_size(width, factor);
        p.crop_height = optimal_crop_size(height, factor);
        p.pad_top = (p.crop_height - height + 1) / 2;
        p.pad_bottom = (p.crop_height - height) / 2;
        p.pad_left = (p.crop_width - width + 1) / 2;
        p.pad_right = (p.crop_width - width) / 2;
        return p;
    }

    /// @brief Pads a CV_32FC1 image (HxW) to crop size using reflection.
    /// Uses BORDER_REFLECT_101 to match PyTorch's ReflectionPad2d semantics
    /// (edge sample is NOT repeated: fedcba|abcdefgh|gfedcba).
    cv::Mat pad(const cv::Mat& img) const {
        cv::Mat padded;
        cv::copyMakeBorder(img, padded,
                           pad_top, pad_bottom, pad_left, pad_right,
                           cv::BORDER_REFLECT_101);
        return padded;
    }

    /// @brief Crops the center region back to the original sensor size.
    cv::Mat crop(const cv::Mat& img) const {
        const int cx = crop_width / 2;
        const int cy = crop_height / 2;
        const int x0 = cx - width / 2;
        const int y0 = cy - height / 2;
        return img(cv::Rect(x0, y0, width, height)).clone();
    }

private:
    static int optimal_crop_size(int max_size, int factor) {
        int crop = factor;
        while (crop < max_size) crop += factor;
        return crop;
    }
};

/// @brief E2VID model inference wrapper with ONNX Runtime and heuristic backend.
class E2VIDInference {
public:
    /// @brief Constructs the inference engine.
    /// @param width,height Sensor dimensions.
    /// @param num_bins Number of event tensor temporal bins (E2VID default: 5).
    /// @param num_encoders UNet encoder depth (default: 4).
    E2VIDInference(int width, int height, int num_bins = 5,
                   int num_encoders = 4)
        : width_(width), height_(height),
          num_bins_(clamp_bins(num_bins)),
          num_encoders_(num_encoders),
          crop_(E2VIDCropParams::compute(width, height, num_encoders)),
          voxel_grid_(width, height, num_bins_) {}

    /// @brief Loads an ONNX model from file.
    /// @return true if the model was loaded successfully.
#if defined(GUI_ALGO_HAS_ONNXRUNTIME)
    bool load_model(const std::string& model_path) {
        try {
            env_ = std::make_unique<Ort::Env>(
                ORT_LOGGING_LEVEL_WARNING, "e2vid");
            Ort::SessionOptions session_opts;
            session_opts.SetIntraOpNumThreads(1);
            session_opts.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
            session_ = std::make_unique<Ort::Session>(
                *env_, model_path.c_str(), session_opts);
            model_path_ = model_path;
            model_loaded_ = true;
            return true;
        } catch (const Ort::Exception&) {
            model_loaded_ = false;
            return false;
        }
    }
#else
    bool load_model(const std::string& model_path) {
        model_path_ = model_path;
        model_loaded_ = false;  // ONNX Runtime not available
        return false;
    }
#endif

    /// @brief Returns true if a model is loaded and ready for inference.
    bool is_model_loaded() const { return model_loaded_; }

    /// @brief Runs inference on a batch of events.
    /// @param events Event array.
    /// @param n Number of events.
    /// @return ONNX path: CV_32FC1 padded image in [0,1] (crop_h x crop_w) —
    ///         the caller is responsible for cropping back to sensor size.
    ///         Heuristic path: CV_8UC1 sensor-sized image in [0,255].
    cv::Mat infer(const Event* events, std::size_t n) {
        if (events == nullptr || n == 0 || width_ <= 0 || height_ <= 0) {
            return cv::Mat::zeros(height_, width_, CV_8UC1);
        }

        // 1. Build voxel grid.
        voxel_grid_.build(events, n);
        if (normalize_input_) {
            voxel_grid_.normalize();
        }

#if defined(GUI_ALGO_HAS_ONNXRUNTIME)
        if (model_loaded_ && session_) {
            return infer_onnx();
        }
#endif
        // Fallback: heuristic reconstruction from voxel grid.
        return infer_heuristic();
    }

    /// @brief Crops a padded image back to the sensor dimensions.
    /// No-op if the image already matches the sensor size (heuristic path).
    cv::Mat crop_to_sensor(const cv::Mat& img) const {
        if (img.rows == height_ && img.cols == width_) {
            return img;
        }
        return crop_.crop(img);
    }

    /// @brief Sets whether to normalize the input voxel grid.
    void set_normalize_input(bool v) { normalize_input_ = v; }
    bool normalize_input() const { return normalize_input_; }

    /// @brief Sets the hot-pixel mask for the voxel grid preprocessor.
    void set_hot_pixel_mask(const std::vector<std::uint8_t>& mask) {
        hot_pixel_mask_ = mask;  // cache so it survives num_bins changes
        voxel_grid_.set_hot_pixel_mask(mask);
    }
    void clear_hot_pixel_mask() {
        hot_pixel_mask_.clear();
        voxel_grid_.clear_hot_pixel_mask();
    }

    void set_num_bins(int b) {
        const int clamped = clamp_bins(b);
        num_bins_ = clamped;
        // Reconstruct voxel grid but preserve the hot-pixel mask.
        voxel_grid_ = EventVoxelGrid(width_, height_, clamped);
        if (!hot_pixel_mask_.empty()) {
            voxel_grid_.set_hot_pixel_mask(hot_pixel_mask_);
        }
    }
    int num_bins() const { return num_bins_; }

    const std::string& model_path() const { return model_path_; }

    void reset() {
        voxel_grid_.reset();
#if defined(GUI_ALGO_HAS_ONNXRUNTIME)
        // Clear recurrent states if applicable.
        prev_states_.clear();
#endif
    }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    static int clamp_bins(int b) {
        // Match EventVoxelGrid's clamp range: [1, 20].
        if (b < 1) return 1;
        if (b > 20) return 20;
        return b;
    }

#if defined(GUI_ALGO_HAS_ONNXRUNTIME)
    /// @brief ONNX Runtime inference path.
    /// Returns the padded CV_32FC1 image in [0,1] (crop_h x crop_w).
    /// The caller crops it back to sensor size after postprocessing.
    /// Handles both plain UNet (1 input/1 output) and UNetRecurrent
    /// (N inputs/M outputs) by feeding zero-initialized states on the first
    /// call and persisting returned states across calls (matches rpg_e2vid's
    /// prev_states handling). Any Ort::Exception falls back to heuristic.
    cv::Mat infer_onnx() {
        const int ch = crop_.crop_height;
        const int cw = crop_.crop_width;

        try {
            // Build input tensor: 1 x num_bins x crop_height x crop_width.
            std::vector<float> input_tensor(
                static_cast<std::size_t>(num_bins_) * ch * cw, 0.0f);

            // Copy voxel grid into padded tensor (reflection padding).
            const float* grid = voxel_grid_.data();
            const int stride_hw = width_ * height_;
            for (int b = 0; b < num_bins_; ++b) {
                cv::Mat bin(height_, width_, CV_32FC1,
                            const_cast<float*>(grid + b * stride_hw));
                cv::Mat padded = crop_.pad(bin);
                std::copy(padded.begin<float>(), padded.end<float>(),
                          input_tensor.begin() +
                              static_cast<std::size_t>(b) * ch * cw);
            }

            Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);
            std::array<std::int64_t, 4> input_shape = {1, num_bins_, ch, cw};

            Ort::AllocatorWithDefaultOptions allocator;
            const std::size_t n_inputs = session_->GetInputCount();
            const std::size_t n_outputs = session_->GetOutputCount();

            // Collect input/output names (keep AllocatedStringPtr alive so the
            // raw pointers remain valid for the Run() call).
            std::vector<Ort::AllocatedStringPtr> in_name_owners;
            std::vector<Ort::AllocatedStringPtr> out_name_owners;
            in_name_owners.reserve(n_inputs);
            out_name_owners.reserve(n_outputs);
            std::vector<const char*> input_name_ptrs;
            std::vector<const char*> output_name_ptrs;
            input_name_ptrs.reserve(n_inputs);
            output_name_ptrs.reserve(n_outputs);
            for (std::size_t i = 0; i < n_inputs; ++i) {
                in_name_owners.push_back(
                    session_->GetInputNameAllocated(i, allocator));
                input_name_ptrs.push_back(in_name_owners.back().get());
            }
            for (std::size_t i = 0; i < n_outputs; ++i) {
                out_name_owners.push_back(
                    session_->GetOutputNameAllocated(i, allocator));
                output_name_ptrs.push_back(out_name_owners.back().get());
            }

            // Build input Ort::Values. First input is always the event voxel
            // grid; subsequent inputs (if any) are recurrent state tensors.
            std::vector<Ort::Value> inputs;
            inputs.reserve(n_inputs);
            inputs.push_back(Ort::Value::CreateTensor<float>(
                mem_info, input_tensor.data(), input_tensor.size(),
                input_shape.data(), input_shape.size()));

            // Allocate zero-initialized state buffers for any extra inputs.
            state_buffers_.clear();
            for (std::size_t i = 1; i < n_inputs; ++i) {
                auto info = session_->GetInputTypeInfoAllocated(i, allocator);
                auto tensor_info = info->GetTensorTypeAndShapeInfo();
                auto shape = tensor_info.GetShape();
                std::size_t total = 1;
                for (auto d : shape) {
                    if (d <= 0) d = 1;  // dynamic dim: assume 1
                    total *= static_cast<std::size_t>(d);
                }
                state_buffers_.emplace_back(total, 0.0f);
                inputs.push_back(Ort::Value::CreateTensor<float>(
                    mem_info, state_buffers_.back().data(),
                    state_buffers_.back().size(), shape.data(), shape.size()));
            }
            // If we have prev_states_ from a previous call, replace the zero
            // state tensors with the persisted states.
            if (!prev_states_.empty() && prev_states_.size() == n_inputs - 1) {
                for (std::size_t i = 1; i < n_inputs; ++i) {
                    inputs[i] = std::move(prev_states_[i - 1]);
                }
                prev_states_.clear();
            }

            // Run inference.
            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_name_ptrs.data(), inputs.data(), inputs.size(),
                output_name_ptrs.data(), output_name_ptrs.size());

            // Persist recurrent states (outputs beyond the first image).
            prev_states_.clear();
            for (std::size_t i = 1; i < outputs.size(); ++i) {
                prev_states_.push_back(std::move(outputs[i]));
            }

            // Extract output: 1 x 1 x crop_h x crop_w.
            const float* output_data = outputs[0].GetTensorData<float>();
            auto out_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            const int out_h = static_cast<int>(out_shape[2]);
            const int out_w = static_cast<int>(out_shape[3]);
            cv::Mat output(out_h, out_w, CV_32FC1,
                           const_cast<float*>(output_data));
            return output.clone();  // deep copy (Ort owns the buffer)
        } catch (const Ort::Exception&) {
            // Inference failure (shape mismatch, recurrent state mismatch, etc.):
            // fall back to heuristic so the pipeline does not crash.
            model_loaded_ = false;
            prev_states_.clear();
            return infer_heuristic();
        }
    }
#endif

    /// @brief Heuristic fallback: reconstructs from voxel grid without a model.
    /// Sums bins, applies sigmoid, returns CV_8UC1.
    cv::Mat infer_heuristic() {
        // Sum across bins to get a 2D event count map.
        cv::Mat sum_img(height_, width_, CV_32FC1, cv::Scalar(0.0f));
        const float* grid = voxel_grid_.data();
        const int stride_hw = width_ * height_;
        for (int b = 0; b < num_bins_; ++b) {
            cv::Mat bin(height_, width_, CV_32FC1,
                        const_cast<float*>(grid + b * stride_hw));
            sum_img += bin;
        }
        // Apply sigmoid: out = 1 / (1 + exp(-k * sum))
        // The sum of events is proportional to edge activity; sigmoid maps
        // it to [0, 1] where high activity = bright edges.
        cv::Mat sig;
        const float k = 0.5f;  // gain
        cv::exp(-k * sum_img, sig);
        sig = 1.0f / (1.0f + sig);
        // Scale to [0, 255].
        cv::Mat gray;
        sig.convertTo(gray, CV_8UC1, 255.0);
        return gray;
    }

    int width_;
    int height_;
    int num_bins_;
    int num_encoders_;
    E2VIDCropParams crop_;
    EventVoxelGrid voxel_grid_;
    std::vector<std::uint8_t> hot_pixel_mask_;  ///< Cached for num_bins rebuilds.
    bool normalize_input_{true};
    bool model_loaded_{false};
    std::string model_path_;

#if defined(GUI_ALGO_HAS_ONNXRUNTIME)
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<Ort::Value> prev_states_;  ///< Recurrent states (UNetRecurrent).
    std::vector<std::vector<float>> state_buffers_;  ///< Backing storage for zero-init states.
#endif
};

} // namespace gui_algo

#endif // GUI_ALGO_ANALYTICS_E2VID_E2VID_INFERENCE_H
