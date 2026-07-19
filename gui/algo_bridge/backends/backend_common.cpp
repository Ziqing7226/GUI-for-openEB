// gui/algo_bridge/backends/backend_common.cpp — out-of-line implementation of
// Preprocessor::rebuild_undistort_lut (the only non-inline member). Kept here
// so backend_common.h stays header-only for the cheap helpers, while the
// expensive cv::undistortPoints call (which drags in calib3d) is compiled once.

#include "backend_common.h"

#include <opencv2/calib3d.hpp>

namespace gui {
namespace backend_detail {

void Preprocessor::rebuild_undistort_lut(int roi_x, int roi_y, int factor) {
    undistort_lut_valid_ = false;
    undistort_lut_.clear();
    undistort_eff_w_ = 0;
    undistort_eff_h_ = 0;

    if (undistort_K_.empty() || undistort_dist_.empty()) return;
    if (factor <= 0) factor = 1;
    if (filter_w_ <= 0 || filter_h_ <= 0) return;

    // Effective post-downsample dimensions the algorithm operates on.
    const int eff_w = filter_w_ / factor;
    const int eff_h = filter_h_ / factor;
    if (eff_w <= 0 || eff_h <= 0) return;

    // Adjust K from sensor resolution to the post-ROI, post-downsample
    // coordinate system the algorithm sees:
    //   cx' = (cx - roi_x) / factor
    //   cy' = (cy - roi_y) / factor
    //   fx' = fx / factor,  fy' = fy / factor
    // cv::undistortPoints with P = K_adj returns undistorted pixels in the
    // same adjusted system, which is exactly the LUT index space — so the
    // result can be indexed directly by post-downsample event coordinates.
    cv::Mat K_adj = undistort_K_.clone();
    if (K_adj.type() != CV_64F) K_adj.convertTo(K_adj, CV_64F);
    cv::Mat dist = undistort_dist_;
    if (dist.type() != CV_64F) dist.convertTo(dist, CV_64F);

    const double f = static_cast<double>(factor);
    K_adj.at<double>(0, 0) /= f;
    K_adj.at<double>(1, 1) /= f;
    K_adj.at<double>(0, 2) = (K_adj.at<double>(0, 2) - roi_x) / f;
    K_adj.at<double>(1, 2) = (K_adj.at<double>(1, 2) - roi_y) / f;

    // Build the input point list: every pixel in the effective grid.
    std::vector<cv::Point2f> pts;
    pts.reserve(static_cast<std::size_t>(eff_w) * eff_h);
    for (int y = 0; y < eff_h; ++y) {
        for (int x = 0; x < eff_w; ++x) {
            pts.emplace_back(static_cast<float>(x), static_cast<float>(y));
        }
    }

    // Forward map: distorted → undistorted. P = K_adj so output is in pixels
    // of the adjusted system. Out-of-bounds results are dropped by apply().
    std::vector<cv::Point2f> undistorted;
    cv::undistortPoints(pts, undistorted, K_adj, dist, cv::noArray(), K_adj);

    undistort_lut_.swap(undistorted);
    undistort_eff_w_ = eff_w;
    undistort_eff_h_ = eff_h;
    undistort_lut_valid_ = true;

    last_roi_x_ = roi_x;
    last_roi_y_ = roi_y;
    last_factor_ = factor;
}

} // namespace backend_detail
} // namespace gui
