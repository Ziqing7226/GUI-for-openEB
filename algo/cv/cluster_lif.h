// algo/cv/cluster_lif.h — LIF neuron grid clustering.
//
// Implements design §4.3.18 (jAER ClusterBubbles / IFSignedNeuronArray). Each
// pixel is modelled as a leaky integrate-and-fire neuron: events increment (ON)
// or decrement (OFF) the membrane potential, which leaks exponentially toward
// the reset value with time constant tau. When a pixel's potential crosses the
// firing threshold it emits a cluster (spike) and resets. Reuses
// algo/common/lif_integrator.h for the neuron array. Header-only.

#ifndef GUI_ALGO_CV_CLUSTER_LIF_H
#define GUI_ALGO_CV_CLUSTER_LIF_H

#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"
#include "algo/common/lif_integrator.h"

namespace gui_algo {

/// @brief A cluster emitted when a LIF neuron fires.
struct LifCluster {
    cv::Point2f position;
    double potential{0.0};  ///< Membrane potential at firing (== threshold).
    int track_id{-1};       ///< Persistent track id, -1 if untracked.
};

/// @brief LIF neuron grid clustering.
class ClusterLIF {
public:
    ClusterLIF(int width, int height,
               float tau_ms = 10.0f,
               float threshold = 1.0f,
               float reset_value = 0.0f)
        : width_(width), height_(height),
          tau_us_(static_cast<Metavision::timestamp>(tau_ms * 1000.0f)),
          threshold_(static_cast<double>(threshold)),
          reset_value_(static_cast<double>(reset_value)),
          lif_(width, height,
               static_cast<Metavision::timestamp>(tau_ms * 1000.0f),
               static_cast<double>(threshold),
               static_cast<double>(reset_value)),
          track_tol_px_(10.0f) {}

    /// @brief Processes an event packet and returns clusters for firing neurons.
    std::vector<LifCluster> process(const EventPacket& packet) {
        std::vector<LifCluster> result;
        if (packet.empty()) return result;
        for (const Event& e : packet) {
            if (e.x >= width_ || e.y >= height_) continue;
            const bool fired = lif_.add_event(e.x, e.y, e.p, e.t);
            if (!fired) continue;
            LifCluster c;
            c.position = cv::Point2f(static_cast<float>(e.x),
                                     static_cast<float>(e.y));
            c.potential = threshold_;
            c.track_id = associate(c);
            result.push_back(c);
        }
        return result;
    }

    // Parameter accessors ---------------------------------------------------
    float tau_ms() const {
        return static_cast<float>(tau_us_) / 1000.0f;
    }
    float threshold() const { return static_cast<float>(threshold_); }
    float reset_value() const { return static_cast<float>(reset_value_); }
    void set_tau_ms(float v) {
        tau_us_ = static_cast<Metavision::timestamp>(v * 1000.0f);
        lif_.set_tau_us(tau_us_);
    }
    void set_threshold(float v) {
        threshold_ = static_cast<double>(v);
        lif_.set_threshold(threshold_);
    }
    void set_reset_value(float v) { reset_value_ = static_cast<double>(v); }

    /// @brief Returns the membrane-potential grid (CV_32F-compatible layout).
    const std::vector<double>& potential_grid() const {
        return lif_.potential_grid();
    }

    void reset() {
        lif_.clear();
        tracks_.clear();
        next_track_id_ = 0;
    }

private:
    struct Track {
        int id{-1};
        cv::Point2f last_position;
    };

    int associate(const LifCluster& c) {
        const float tol2 = track_tol_px_ * track_tol_px_;
        int best_id = -1;
        float best_d2 = tol2;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const float dx = tracks_[i].last_position.x - c.position.x;
            const float dy = tracks_[i].last_position.y - c.position.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best_id = tracks_[i].id; }
        }
        if (best_id < 0) {
            best_id = next_track_id_++;
            tracks_.push_back(Track{best_id, c.position});
        } else {
            for (auto& tr : tracks_) {
                if (tr.id == best_id) { tr.last_position = c.position; break; }
            }
        }
        return best_id;
    }

    int width_;
    int height_;
    Metavision::timestamp tau_us_;
    double threshold_;
    double reset_value_;
    LifIntegrator lif_;
    float track_tol_px_;
    std::vector<Track> tracks_;
    int next_track_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_CLUSTER_LIF_H
