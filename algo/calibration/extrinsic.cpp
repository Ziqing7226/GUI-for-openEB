// algo/calibration/extrinsic.cpp

#include "extrinsic.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace gui_algo {

ExtrinsicCalibration::ExtrinsicCalibration() = default;
ExtrinsicCalibration::~ExtrinsicCalibration() = default;

void ExtrinsicCalibration::set_pattern(CalibrationPattern pattern,
                                       int cols, int rows,
                                       float square_size_mm) {
    cv::Size new_bs = (pattern == CalibrationPattern::Chessboard)
        ? cv::Size(std::max(cols - 1, 1), std::max(rows - 1, 1))
        : cv::Size(std::max(cols, 1), std::max(rows, 1));
    // Same rationale as IntrinsicCalibration: the wizard calls set_pattern
    // on every capture/run, so a mid-session board change would feed
    // cv::stereoCalibrate point sets of inconsistent size/coordinate-system.
    if (pattern_ != pattern || board_size_ != new_bs || square_size_mm_ != square_size_mm) {
        common_points_first_.clear();
        common_points_second_.clear();
        object_points_.clear();
        image_size_first_ = cv::Size(0, 0);
        image_size_second_ = cv::Size(0, 0);
    }
    pattern_ = pattern;
    board_size_ = new_bs;
    square_size_mm_ = square_size_mm;
}

std::vector<cv::Point3f> ExtrinsicCalibration::make_object_grid() const {
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

DetectionResult ExtrinsicCalibration::detect(const cv::Mat& frame,
                                             cv::Size& known_size,
                                             bool annotate) {
    DetectionResult result;
    if (frame.empty()) return result;
    if (known_size.area() == 0) known_size = frame.size();

    cv::Mat gray;
    if (frame.channels() == 1) gray = frame;
    else cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

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
        if (frame.channels() == 1) cv::cvtColor(frame, result.image, cv::COLOR_GRAY2BGR);
        else result.image = frame.clone();
        cv::drawChessboardCorners(result.image, board_size_, corners, found);
    }
    return result;
}

ExtrinsicCalibration::PairResult ExtrinsicCalibration::add_pair(
    const cv::Mat& frame_first, const cv::Mat& frame_second, bool annotate) {
    PairResult pr;
    pr.first = detect(frame_first, image_size_first_, annotate);
    pr.second = detect(frame_second, image_size_second_, annotate);
    pr.both_found = pr.first.found && pr.second.found;
    if (pr.both_found) {
        common_points_first_.push_back(pr.first.points);
        common_points_second_.push_back(pr.second.points);
        object_points_.push_back(make_object_grid());
    }
    return pr;
}

ExtrinsicResult ExtrinsicCalibration::run(const cv::Mat& k1, const cv::Mat& d1,
                                          const cv::Mat& k2, const cv::Mat& d2) {
    ExtrinsicResult result;
    if (common_points_first_.size() < 3) {
        result.error = "Need at least 3 valid pairs; got " +
                       std::to_string(common_points_first_.size());
        return result;
    }
    if (image_size_first_.area() == 0 || image_size_second_.area() == 0) {
        result.error = "Image sizes unknown";
        return result;
    }

    cv::Mat R, T, E, F;
    try {
        double rms = cv::stereoCalibrate(object_points_,
            common_points_first_, common_points_second_,
            k1, d1, k2, d2,
            image_size_first_, R, T, E, F,
            cv::CALIB_FIX_INTRINSIC,
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 1e-6));
        result.ok = true;
        result.rms = rms;
        result.R = R;
        result.T = T;
        result.E = E;
        result.F = F;
        result.pairs_used = common_points_first_.size();
    } catch (const cv::Exception& e) {
        result.error = e.what();
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

void ExtrinsicCalibration::reset() {
    common_points_first_.clear();
    common_points_second_.clear();
    object_points_.clear();
    image_size_first_ = cv::Size(0, 0);
    image_size_second_ = cv::Size(0, 0);
}

} // namespace gui_algo
