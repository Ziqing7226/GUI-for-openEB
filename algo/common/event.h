// algo/common/event.h — EventCD POD wrapper with polarity/coordinate/timestamp accessors.
//
// Zero-overhead abstraction over Metavision::EventCD, corresponding to jAER
// BasicEvent. Provides named accessors and convenience helpers used across the
// self-developed algorithm modules. Layout-compatible with EventCD so it can be
// reinterpret_cast from raw SDK buffers without copying.

#ifndef GUI_ALGO_COMMON_EVENT_H
#define GUI_ALGO_COMMON_EVENT_H

#include <cstdint>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/utils/timestamp.h>

namespace gui_algo {

/// @brief POD wrapper around Metavision::EventCD.
///
/// Layout-compatible with EventCD (x, y, p, t). Adds named accessors and
/// polarity helpers without adding virtual functions or extra storage.
struct Event {
    std::uint16_t x{0};          ///< Column position
    std::uint16_t y{0};          ///< Row position
    short p{0};                  ///< Polarity: 1 = ON (positive contrast), 0 = OFF
    Metavision::timestamp t{0};  ///< Timestamp in microseconds

    Event() = default;
    Event(std::uint16_t x_, std::uint16_t y_, short p_, Metavision::timestamp t_)
        : x(x_), y(y_), p(p_), t(t_) {}

    /// @brief Implicit construction from SDK event (zero-copy view semantics).
    Event(const Metavision::EventCD& e) : x(e.x), y(e.y), p(e.p), t(e.t) {}

    /// @brief Implicit conversion back to SDK event.
    operator Metavision::EventCD() const {
        return Metavision::EventCD(x, y, p, t);
    }

    // Named accessors -------------------------------------------------------

    std::uint16_t col() const { return x; }
    std::uint16_t row() const { return y; }
    Metavision::timestamp time_us() const { return t; }

    /// @brief Polarity as a signed value: +1 for ON, -1 for OFF.
    /// Useful for signed accumulation (e.g. DVS framing, LIF integration).
    int signed_polarity() const { return p ? 1 : -1; }

    bool is_on() const { return p != 0; }
    bool is_off() const { return p == 0; }

    // Comparators (by timestamp, for sorting / windowing) -------------------

    bool operator<(const Event& o) const { return t < o.t; }
    bool operator<=(const Event& o) const { return t <= o.t; }
    bool operator>(const Event& o) const { return t > o.t; }
    bool operator>=(const Event& o) const { return t >= o.t; }
    bool operator==(const Event& o) const {
        return t == o.t && p == o.p && x == o.x && y == o.y;
    }
};

static_assert(sizeof(Event) == sizeof(Metavision::EventCD),
              "Event must be layout-compatible with Metavision::EventCD");

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_EVENT_H
