// algo/cv/overlay.h — Visualization overlay for algorithm results.
//
// Design §4.3.26. Draws overlay primitives (optical-flow arrows, tracked-object
// bboxes, line segments, corner markers, circles, trajectories, text) onto a
// rendered cv::Mat. POD structs are defined locally for the drawn types to
// avoid circular includes with the algorithm modules that produce them.
// Header-only.

#ifndef GUI_ALGO_CV_OVERLAY_H
#define GUI_ALGO_CV_OVERLAY_H

#include <cstddef>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "algo/cv/line_segment_detector.h"  // for LineSegment

namespace gui_algo {

/// @brief Optical flow vector (origin + displacement).
struct FlowVector {
    float x{0.0f};
    float y{0.0f};
    float dx{0.0f};
    float dy{0.0f};
};

/// @brief Tracked object bounding box with an integer ID.
struct TrackedObject {
    int id{0};
    float x{0.0f};   ///< Top-left X
    float y{0.0f};   ///< Top-left Y
    float w{0.0f};   ///< Width
    float h{0.0f};   ///< Height
};

/// @brief Corner marker location.
struct Corner {
    float x{0.0f};
    float y{0.0f};
};

/// @brief Circle marker (center + radius).
struct Circle {
    float cx{0.0f};
    float cy{0.0f};
    float radius{0.0f};
};

/// @brief Draws algorithm result overlays onto a cv::Mat.
class Overlay {
public:
    Overlay() = default;

    /// @brief Draws optical-flow arrows.
    void draw_flow_arrows(cv::Mat& img, const std::vector<FlowVector>& flows,
                          const cv::Scalar& color = cv::Scalar(255, 255, 0),
                          float scale = 1.0f, int thickness = 1) const {
        if (img.empty()) return;
        for (const FlowVector& f : flows) {
            const cv::Point pt1(cvRound(f.x), cvRound(f.y));
            const cv::Point pt2(cvRound(f.x + f.dx * scale),
                                cvRound(f.y + f.dy * scale));
            cv::arrowedLine(img, pt1, pt2, color, thickness, cv::LINE_8, 0, 0.3);
        }
    }

    /// @brief Draws tracked-object bounding boxes with ID labels.
    void draw_bboxes(cv::Mat& img, const std::vector<TrackedObject>& objs,
                     const cv::Scalar& color = cv::Scalar(0, 255, 255),
                     int thickness = 2) const {
        if (img.empty()) return;
        for (const TrackedObject& o : objs) {
            const cv::Rect rect(cvRound(o.x), cvRound(o.y),
                                cvRound(o.w), cvRound(o.h));
            cv::rectangle(img, rect, color, thickness);
            cv::putText(img, std::to_string(o.id),
                        cv::Point(rect.x, std::max(0, rect.y - 4)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, color, 1);
        }
    }

    /// @brief Draws line segments.
    void draw_lines(cv::Mat& img, const std::vector<LineSegment>& lines,
                    const cv::Scalar& color = cv::Scalar(0, 255, 0),
                    int thickness = 1) const {
        if (img.empty()) return;
        for (const LineSegment& l : lines) {
            cv::line(img,
                     cv::Point(cvRound(l.start.x), cvRound(l.start.y)),
                     cv::Point(cvRound(l.end.x), cvRound(l.end.y)),
                     color, thickness, cv::LINE_8);
        }
    }

    /// @brief Draws corner markers (small crosses).
    void draw_corners(cv::Mat& img, const std::vector<Corner>& corners,
                      const cv::Scalar& color = cv::Scalar(0, 0, 255),
                      int size = 3, int thickness = 1) const {
        if (img.empty()) return;
        for (const Corner& c : corners) {
            const int x = cvRound(c.x);
            const int y = cvRound(c.y);
            cv::line(img, cv::Point(x - size, y), cv::Point(x + size, y),
                     color, thickness, cv::LINE_8);
            cv::line(img, cv::Point(x, y - size), cv::Point(x, y + size),
                     color, thickness, cv::LINE_8);
        }
    }

    /// @brief Draws circles.
    void draw_circles(cv::Mat& img, const std::vector<Circle>& circles,
                      const cv::Scalar& color = cv::Scalar(255, 0, 0),
                      int thickness = 1) const {
        if (img.empty()) return;
        for (const Circle& c : circles) {
            cv::circle(img, cv::Point(cvRound(c.cx), cvRound(c.cy)),
                       cvRound(c.radius), color, thickness, cv::LINE_8);
        }
    }

    /// @brief Draws text at the given position.
    void draw_text(cv::Mat& img, const std::string& text, const cv::Point& pos,
                   const cv::Scalar& color = cv::Scalar(255, 255, 255),
                   double scale = 0.4, int thickness = 1) const {
        if (img.empty()) return;
        cv::putText(img, text, pos, cv::FONT_HERSHEY_SIMPLEX, scale, color,
                    thickness, cv::LINE_8);
    }

    /// @brief Draws a polyline trajectory.
    void draw_trajectory(cv::Mat& img, const std::vector<cv::Point2f>& traj,
                         const cv::Scalar& color = cv::Scalar(0, 255, 255),
                         int thickness = 1) const {
        if (img.empty() || traj.size() < 2) return;
        for (std::size_t i = 1; i < traj.size(); ++i) {
            cv::line(img,
                     cv::Point(cvRound(traj[i - 1].x), cvRound(traj[i - 1].y)),
                     cv::Point(cvRound(traj[i].x), cvRound(traj[i].y)),
                     color, thickness, cv::LINE_8);
        }
    }
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_OVERLAY_H
