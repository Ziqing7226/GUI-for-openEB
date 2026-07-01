// algo/common/event_packet.h — span-style zero-copy event packet.
//
// Wraps a contiguous range of events (Metavision::EventCD or gui_algo::Event)
// as a non-owning view, analogous to std::span. Algorithms receive packets
// rather than raw pointer/size pairs for safer, bounds-checked iteration.
// The view is immutable by default; mutable variants are provided for in-place
// filters.

#ifndef GUI_ALGO_COMMON_EVENT_PACKET_H
#define GUI_ALGO_COMMON_EVENT_PACKET_H

#include <cstddef>
#include <iterator>
#include <type_traits>

#include "event.h"

#include <metavision/sdk/base/events/event_cd.h>

namespace gui_algo {

/// @brief Non-owning view over a contiguous event range.
///
/// @tparam EventT Event type (must be layout-compatible with EventCD).
template <typename EventT>
class EventPacketBase {
public:
    using value_type = EventT;
    using pointer = EventT*;
    using const_pointer = const EventT*;
    using reference = EventT&;
    using const_reference = const EventT&;
    using iterator = pointer;
    using const_iterator = const_pointer;

    EventPacketBase() = default;

    EventPacketBase(pointer begin, std::size_t n) : begin_(begin), size_(n) {}

    EventPacketBase(pointer begin, pointer end)
        : begin_(begin), size_(static_cast<std::size_t>(end - begin)) {}

    // Iterators -------------------------------------------------------------
    iterator begin() { return begin_; }
    iterator end() { return begin_ + size_; }
    const_iterator begin() const { return begin_; }
    const_iterator end() const { return begin_ + size_; }
    const_iterator cbegin() const { return begin_; }
    const_iterator cend() const { return begin_ + size_; }

    // Element access --------------------------------------------------------
    reference operator[](std::size_t i) { return begin_[i]; }
    const_reference operator[](std::size_t i) const { return begin_[i]; }
    pointer data() { return begin_; }
    const_pointer data() const { return begin_; }

    // Capacity --------------------------------------------------------------
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    /// @brief Returns a sub-packet [start, start+count).
    EventPacketBase subpacket(std::size_t start, std::size_t count) const {
        return EventPacketBase(begin_ + start,
                               count < (size_ - start) ? count : (size_ - start));
    }

    /// @brief First @p n events.
    EventPacketBase first(std::size_t n) const {
        return subpacket(0, n);
    }

    /// @brief Last @p n events.
    EventPacketBase last(std::size_t n) const {
        return subpacket(n < size_ ? size_ - n : 0, n);
    }

private:
    pointer begin_{nullptr};
    std::size_t size_{0};
};

/// @brief Immutable event packet (most common usage).
using EventPacket = EventPacketBase<const Event>;

/// @brief Mutable event packet (for in-place filters that rewrite events).
using MutableEventPacket = EventPacketBase<Event>;

/// @brief Immutable packet over raw SDK events (zero-copy from HAL/decoders).
using EventCDPacket = EventPacketBase<const Metavision::EventCD>;

/// @brief Mutable packet over raw SDK events.
using MutableEventCDPacket = EventPacketBase<Metavision::EventCD>;

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_EVENT_PACKET_H
