// algo/calibration/intrinsic.h — intrinsic camera calibration (Zhang method).
//
// Phase 9 (design §4.5.1): detects chessboard / circle-grid corners in
// accumulated event frames, accumulates multi-pose observations, and runs
// cv::calibrateCamera to produce K, distCoeffs and an RMS reprojection error.
//
// The algorithm is frame-driven: the GUI wizard feeds accumulated grayscale
// frames via add_frame(). Detection runs synchronously on the caller's thread
// (cheap relative to calibration itself). Once enough poses are collected,
// run() performs the bundle adjustment.

#ifndef GUI_ALGO_CALIBRATION_INTRINSIC_H
#define GUI_ALGO_CALIBRATION_INTRINSIC_H

#include <cstddef>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace gui_algo {

/// @brief Pattern type for calibration board detection.
enum class CalibrationPattern {
    Chessboard,     ///< Standard chessboard (inner corners)
    CircleGrid,     ///< Regular circle grid
    AsymmetricCircles ///< Asymmetric circle grid
};

/// @brief Result of a single frame's corner detection.
struct DetectionResult {
    bool found{false};
    cv::Mat image;                 ///< Annotated copy (for wizard preview)
    std::vector<cv::Point2f> points; ///< Detected corner coordinates
};

/// @brief Result of the full intrinsic calibration.
struct IntrinsicResult {
    bool ok{false};
    double rms{0.0};               ///< Reprojection RMS (pixels)
    cv::Mat K;                     ///< 3x3 camera matrix
    cv::Mat dist_coeffs;           ///< 1x5 distortion [k1,k2,p1,p2,k3]
    std::vector<cv::Mat> rvecs;    ///< Per-frame rotation vectors
    std::vector<cv::Mat> tvecs;    ///< Per-frame translation vectors
    std::size_t frames_used{0};
    std::string error;             ///< Empty when ok==true
    cv::Mat undistort_map_x;       ///< Precomputed undistort LUT (x), design §4.5.1
    cv::Mat undistort_map_y;       ///< Precomputed undistort LUT (y), design §4.5.1
};

/// @brief Intrinsic calibration accumulator.
class IntrinsicCalibration {
public:
    IntrinsicCalibration();
    ~IntrinsicCalibration();

    /// @brief Configures the board geometry. Must be called before add_frame().
    void set_pattern(CalibrationPattern pattern,
                     int cols, int rows,
                     float square_size_mm);

    /// @brief Attempts to detect the calibration pattern in @p frame.
    /// @param frame Grayscale or BGR accumulated event frame.
    /// @param annotate If true, @p result.image contains a drawn preview.
    /// @return DetectionResult with found state and corner points.
    DetectionResult add_frame(const cv::Mat& frame, bool annotate = true);

    /// @brief Runs cv::calibrateCamera on all collected observations.
    /// On success, precomputes the undistort LUT (design §4.5.1).
    IntrinsicResult run();

    /// @brief Discards all collected observations.
    void reset();

    std::size_t frame_count() const { return image_points_.size(); }
    cv::Size board_size() const { return board_size_; }
    cv::Size image_size() const { return image_size_; }

    /// @brief Precomputes the HxW undistort LUT via cv::initUndistortRectifyMap.
    /// Fills undistort_map_x_ / undistort_map_y_ for O(1) runtime remap.
    /// @param image_size Output image size for the LUT.
    void precompute_undistort_lut(cv::Size image_size);

    /// @brief Accessor for the precomputed x-map (empty until run/precompute).
    const cv::Mat& undistort_map_x() const { return undistort_map_x_; }
    /// @brief Accessor for the precomputed y-map (empty until run/precompute).
    const cv::Mat& undistort_map_y() const { return undistort_map_y_; }

private:
    CalibrationPattern pattern_{CalibrationPattern::Chessboard};
    cv::Size board_size_{0, 0};    ///< (cols-1, rows-1) for chessboard, (cols, rows) for circles
    float square_size_mm_{1.0f};
    cv::Size image_size_{0, 0};

    std::vector<std::vector<cv::Point2f>> image_points_;
    std::vector<std::vector<cv::Point3f>> object_points_;

    cv::Mat K_;                ///< Cached camera matrix (from last successful run)
    cv::Mat dist_coeffs_;      ///< Cached distortion coefficients (from last run)
    cv::Mat undistort_map_x_;  ///< Precomputed undistort LUT (x), design §4.5.1
    cv::Mat undistort_map_y_;  ///< Precomputed undistort LUT (y), design §4.5.1

    std::vector<cv::Point3f> make_object_grid() const;
};

} // namespace gui_algo

#endif // GUI_ALGO_CALIBRATION_INTRINSIC_H
