// algo/calibration/extrinsic.h — stereo / multi-camera extrinsic calibration.
//
// Phase 9 (design §4.5.2): given two intrinsic calibrations and synchronized
// frame pairs, computes the rigid transform (R, T) between the two cameras
// via cv::stereoCalibrate. Produces R, T, E, F matrices.

#ifndef GUI_ALGO_CALIBRATION_EXTRINSIC_H
#define GUI_ALGO_CALIBRATION_EXTRINSIC_H

#include <cstddef>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "intrinsic.h"

namespace gui_algo {

/// @brief Result of stereo extrinsic calibration.
struct ExtrinsicResult {
    bool ok{false};
    double rms{0.0};
    cv::Mat R;   ///< 3x3 rotation from cam1 to cam2
    cv::Mat T;   ///< 3x1 translation from cam1 to cam2
    cv::Mat E;   ///< Essential matrix
    cv::Mat F;   ///< Fundamental matrix
    std::size_t pairs_used{0};
    std::string error;
};

/// @brief Stereo extrinsic calibrator.
///
/// Usage: feed synchronized frame pairs via add_pair() (the same board must be
/// visible in both). After enough pairs, call run() with both intrinsics.
class ExtrinsicCalibration {
public:
    ExtrinsicCalibration();
    ~ExtrinsicCalibration();

    void set_pattern(CalibrationPattern pattern,
                     int cols, int rows,
                     float square_size_mm);

    /// @brief Detects the pattern in both frames and stores the pair if both
    /// detections succeed. Returns per-camera detection results.
    /// @note frames are assumed time-synchronized (design §4.5.2).
    struct PairResult {
        DetectionResult first;
        DetectionResult second;
        bool both_found{false};
    };
    PairResult add_pair(const cv::Mat& frame_first,
                        const cv::Mat& frame_second,
                        bool annotate = true);

    /// @brief Runs cv::stereoCalibrate.
    /// @param k1,d1 Intrinsic K and distortion of camera 1.
    /// @param k2,d2 Intrinsic K and distortion of camera 2.
    ExtrinsicResult run(const cv::Mat& k1, const cv::Mat& d1,
                        const cv::Mat& k2, const cv::Mat& d2);

    void reset();

    std::size_t pair_count() const { return common_points_first_.size(); }

private:
    CalibrationPattern pattern_{CalibrationPattern::Chessboard};
    cv::Size board_size_{0, 0};
    float square_size_mm_{1.0f};
    cv::Size image_size_first_{0, 0};
    cv::Size image_size_second_{0, 0};

    std::vector<std::vector<cv::Point2f>> common_points_first_;
    std::vector<std::vector<cv::Point2f>> common_points_second_;
    std::vector<std::vector<cv::Point3f>> object_points_;

    std::vector<cv::Point3f> make_object_grid() const;
    DetectionResult detect(const cv::Mat& frame, cv::Size& known_size,
                           bool annotate);
};

} // namespace gui_algo

#endif // GUI_ALGO_CALIBRATION_EXTRINSIC_H
