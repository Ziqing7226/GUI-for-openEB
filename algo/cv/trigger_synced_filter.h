// algo/cv/trigger_synced_filter.h — Trigger-synced event filtering.
//
// Implements design §4.3.21 (jAER DataSynchronizerFromTriggers). Keeps only
// events falling within an external-trigger window: [trigger_t, trigger_t +
// trigger_window_us] on the selected trigger channel. Events outside all
// trigger windows are discarded as out-of-sync noise. Used for multi-sensor
// synchronised acquisition. Header-only.

#ifndef GUI_ALGO_CV_TRIGGER_SYNCED_FILTER_H
#define GUI_ALGO_CV_TRIGGER_SYNCED_FILTER_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief Filters events to keep only those within trigger windows.
class TriggerSyncedFilter {
public:
    static constexpr int kNumChannels = 8;

    TriggerSyncedFilter(Metavision::timestamp trigger_window_us = 100000,
                        int trigger_channel = 0)
        : trigger_window_us_(trigger_window_us),
          trigger_channel_(clamp_channel(trigger_channel)) {}

    /// @brief Registers an external trigger edge.
    /// @param t Trigger timestamp (us).
    /// @param channel Trigger channel [0, 7].
    void add_trigger(Metavision::timestamp t, int channel) {
        if (channel < 0 || channel >= kNumChannels) return;
        triggers_[static_cast<std::size_t>(channel)].push_back(t);
    }

    /// @brief Returns the subset of @p packet within trigger windows.
    std::vector<Event> process(const EventPacket& packet) {
        std::vector<Event> kept;
        const std::vector<Metavision::timestamp>& trigs =
            triggers_[static_cast<std::size_t>(trigger_channel_)];
        if (trigs.empty()) return kept;
        kept.reserve(packet.size());
        for (const Event& e : packet) {
            // Last trigger with t <= e.t (triggers arrive in time order).
            auto it = std::upper_bound(trigs.begin(), trigs.end(), e.t);
            if (it == trigs.begin()) continue;
            --it;
            if (e.t - *it <= trigger_window_us_) {
                kept.push_back(e);
            }
        }
        return kept;
    }

    // Parameter accessors ---------------------------------------------------
    Metavision::timestamp trigger_window_us() const { return trigger_window_us_; }
    int trigger_channel() const { return trigger_channel_; }
    void set_trigger_window_us(Metavision::timestamp v) { trigger_window_us_ = v; }
    void set_trigger_channel(int v) { trigger_channel_ = clamp_channel(v); }

    void reset() {
        for (auto& ch : triggers_) ch.clear();
    }

private:
    static int clamp_channel(int ch) {
        if (ch < 0) return 0;
        if (ch >= kNumChannels) return kNumChannels - 1;
        return ch;
    }

    Metavision::timestamp trigger_window_us_;
    int trigger_channel_;
    std::array<std::vector<Metavision::timestamp>, kNumChannels> triggers_;
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_TRIGGER_SYNCED_FILTER_H
