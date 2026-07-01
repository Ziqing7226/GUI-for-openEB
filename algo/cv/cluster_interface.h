// algo/cv/cluster_interface.h — abstract base for event-level clustering/tracking.
//
// Defines the ClusterInterface abstract class consumed by the object tracker
// (design §4.3.11). Concrete implementations (RCT, Median, Kalman,
// MultiHypothesis) live in object_tracker.h. Each cluster owns a position,
// velocity, bounding box, trajectory (vector<ClusterPathPoint>), mass (event
// count) and age, and supports update-by-event, distance-to-event, visibility
// queries, and temporal ageing. Mirrors jAER ClusterInterface.

#ifndef GUI_ALGO_CV_CLUSTER_INTERFACE_H
#define GUI_ALGO_CV_CLUSTER_INTERFACE_H

#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/cv/cluster_path_point.h"

namespace gui_algo {

/// @brief Abstract base class for event-driven clusters/tracks.
class ClusterInterface {
public:
    virtual ~ClusterInterface() = default;

    /// @brief Updates the cluster with a new event assumed to belong to it.
    virtual void update(const Event& e) = 0;

    /// @brief Spatial distance from this cluster to an event (px).
    virtual float distance(const Event& e) const = 0;

    /// @brief Whether the cluster is currently visible (recently updated).
    virtual bool is_visible() const = 0;

    /// @brief Advances cluster state by @p dt_us microseconds (aging/prediction).
    virtual void age(Metavision::timestamp dt_us) = 0;

    // Getters ---------------------------------------------------------------
    virtual float x() const = 0;
    virtual float y() const = 0;
    virtual float vx() const = 0;
    virtual float vy() const = 0;
    virtual cv::Rect bbox() const = 0;
    virtual const std::vector<ClusterPathPoint>& trajectory() const = 0;
    virtual float mass() const = 0;
    virtual Metavision::timestamp age_us() const = 0;
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_CLUSTER_INTERFACE_H
