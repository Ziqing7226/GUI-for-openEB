// algo/cv/cluster_path_point.h — trajectory sample for cluster/object tracking.
//
// POD struct holding a single point along a cluster or tracked-object
// trajectory: position, velocity, timestamp and instantaneous radius.
// Mirrors jAER ClusterPathPoint used by RectangularClusterTracker and the
// event-level object tracker (design §4.3.11). Header-only; trivially copyable.

#ifndef GUI_ALGO_CV_CLUSTER_PATH_POINT_H
#define GUI_ALGO_CV_CLUSTER_PATH_POINT_H

#include <metavision/sdk/base/utils/timestamp.h>

namespace gui_algo {

/// @brief Single sample along a cluster / tracked-object trajectory.
struct ClusterPathPoint {
    float x{0.0F};                  ///< Centroid x (px)
    float y{0.0F};                  ///< Centroid y (px)
    float vx{0.0F};                 ///< Velocity x (px/s)
    float vy{0.0F};                 ///< Velocity y (px/s)
    Metavision::timestamp t{0};     ///< Timestamp (us)
    float radius{0.0F};             ///< Instantaneous cluster radius (px)

    ClusterPathPoint() = default;
    ClusterPathPoint(float x_, float y_, float vx_, float vy_,
                     Metavision::timestamp t_, float radius_)
        : x(x_), y(y_), vx(vx_), vy(vy_), t(t_), radius(radius_) {}
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_CLUSTER_PATH_POINT_H
