// algo/calibration/intrinsic.cpp

#include "intrinsic.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace gui_algo {

IntrinsicCalibration::IntrinsicCalibration() = default;
IntrinsicCalibration::~IntrinsicCalibration() = default;

void IntrinsicCalibration::set_pattern(CalibrationPattern pattern,
                                       int cols, int rows,
                                       float square_size_mm) {
    // For chessboard, OpenCV expects inner-corner count (cols-1, rows-1).
    // For circle grids, the count is the number of circles per row/column.
    cv::Size new_bs = (pattern == CalibrationPattern::Chessboard)
        ? cv::Size(std::max(cols - 1, 1), std::max(rows - 1, 1))
        : cv::Size(std::max(cols, 1), std::max(rows, 1));
    // The wizard refreshes set_pattern on every capture/run so the user can
    // change pattern/dims/square mid-session. Each frame's object_points_ is
    // frozen at capture time via make_object_grid(), so mixing different
    // board geometries would feed cv::calibrateCamera point sets of
    // inconsistent size/coordinate-system and either throw or return a
    // wrong result. Clear accumulated observations when the geometry changes.
    if (pattern_ != pattern || board_size_ != new_bs || square_size_mm_ != square_size_mm) {
        image_points_.clear();
        object_points_.clear();
        image_size_ = cv::Size(0, 0);
    }
    pattern_ = pattern;
    board_size_ = new_bs;
    square_size_mm_ = square_size_mm;
}

std::vector<cv::Point3f> IntrinsicCalibration::make_object_grid() const {
    std::vector<cv::Point3f> pts;
    pts.reserve(static_cast<std::size_t>(board_size_.width) *
                static_cast<std::size_t>(board_size_.height));
    if (pattern_ == CalibrationPattern::AsymmetricCircles) {
        for (int r = 0; r < board_size_.height; ++r) {
            for (int c = 0; c < board_size_.width; ++c) {
                pts.emplace_back(
                    c * square_size_mm_,
                    (2 * r + (c & 1)) * square_size_mm_,
                    0.0f);
            }
        }
    } else {
        for (int r = 0; r < board_size_.height; ++r) {
            for (int c = 0; c < board_size_.width; ++c) {
                pts.emplace_back(c * square_size_mm_, r * square_size_mm_, 0.0f);
            }
        }
    }
    return pts;
}

DetectionResult IntrinsicCalibration::add_frame(const cv::Mat& frame, bool annotate) {
    DetectionResult result;
    if (frame.empty()) {
        result.found = false;
        return result;
    }
    if (image_size_.area() == 0) {
        image_size_ = frame.size();
    }

    cv::Mat gray;
    if (frame.channels() == 1) {
        gray = frame;
    } else {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    }

    std::vector<cv::Point2f> corners;
    bool found = false;

    switch (pattern_) {
        case CalibrationPattern::Chessboard:
            found = cv::findChessboardCorners(gray, board_size_, corners,
                cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
            if (found) {
                cv::cornerSubPix(gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
                    cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                                      30, 0.01));
            }
            break;
        case CalibrationPattern::CircleGrid:
            found = cv::findCirclesGrid(gray, board_size_, corners);
            break;
        case CalibrationPattern::AsymmetricCircles:
            found = cv::findCirclesGrid(gray, board_size_, corners,
                cv::CALIB_CB_ASYMMETRIC_GRID);
            break;
    }

    result.found = found;
    result.points = corners;

    if (annotate) {
        if (frame.channels() == 1) {
            cv::cvtColor(frame, result.image, cv::COLOR_GRAY2BGR);
        } else {
            result.image = frame.clone();
        }
        cv::drawChessboardCorners(result.image, board_size_, corners, found);
    }

    if (found) {
        image_points_.push_back(corners);
        object_points_.push_back(make_object_grid());
    }
    return result;
}

IntrinsicResult IntrinsicCalibration::run() {
    IntrinsicResult result;
    if (image_points_.size() < 3) {
        result.error = "Need at least 3 valid frames; got " +
                       std::to_string(image_points_.size());
        return result;
    }
    if (image_size_.area() == 0) {
        result.error = "Image size not yet known";
        return result;
    }

    cv::Mat K = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat dist = cv::Mat::zeros(1, 5, CV_64F);
    std::vector<cv::Mat> rvecs, tvecs;

    try {
        double rms = cv::calibrateCamera(object_points_, image_points_,
            image_size_, K, dist, rvecs, tvecs);
        result.ok = true;
        result.rms = rms;
        result.K = K;
        result.dist_coeffs = dist;
        result.rvecs = std::move(rvecs);
        result.tvecs = std::move(tvecs);
        result.frames_used = image_points_.size();
    } catch (const cv::Exception& e) {
        result.error = e.what();
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

void IntrinsicCalibration::reset() {
    image_points_.clear();
    object_points_.clear();
    image_size_ = cv::Size(0, 0);
}

} // namespace gui_algo
