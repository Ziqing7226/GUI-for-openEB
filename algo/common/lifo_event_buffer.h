// algo/common/lifo_event_buffer.h — LIFO event stack for recent-event lookback.
//
// Bounded last-in-first-out stack of events, inspired by jAER AEStack. Used by
// algorithms that need to backtrack over the most recent N events (e.g. local
// plane fitting, refractory checks, coincidence windows). Push overwrites the
// oldest entry when full. Thread-compatible: not internally synchronized.

#ifndef GUI_ALGO_COMMON_LIFO_EVENT_BUFFER_H
#define GUI_ALGO_COMMON_LIFO_EVENT_BUFFER_H

#include <cstddef>
#include <vector>

#include "event.h"

namespace gui_algo {

/// @brief Bounded LIFO stack of events (jAER AEStack style).
class LifoEventBuffer {
public:
    explicit LifoEventBuffer(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity), buf_(capacity_) {}

    /// @brief Pushes an event onto the top of the stack. If full, the bottom
    /// (oldest) event is discarded.
    void push(const Event& e) {
        if (size_ < capacity_) {
            buf_[size_] = e;
            ++size_;
        } else {
            // Overwrite oldest: shift down by one, then write at top.
            for (std::size_t i = 1; i < capacity_; ++i) {
                buf_[i - 1] = buf_[i];
            }
            buf_[capacity_ - 1] = e;
        }
    }

    /// @brief Pops the most recent event. Returns false if empty.
    bool pop(Event& out) {
        if (size_ == 0) return false;
        --size_;
        out = buf_[size_];
        return true;
    }

    /// @brief Returns the most recent event without popping (nullptr if empty).
    const Event* top() const {
        return size_ == 0 ? nullptr : &buf_[size_ - 1];
    }

    /// @brief Returns the i-th most recent event (0 = top). nullptr if OOB.
    const Event* at_from_top(std::size_t i) const {
        return i < size_ ? &buf_[size_ - 1 - i] : nullptr;
    }

    /// @brief Iterates from newest to oldest.
    /// @p cb is called as cb(const Event&).
    template <typename F>
    void for_each_newest_first(F&& cb) const {
        for (std::size_t i = 0; i < size_; ++i) {
            cb(buf_[size_ - 1 - i]);
        }
    }

    /// @brief Iterates from oldest to newest.
    template <typename F>
    void for_each_oldest_first(F&& cb) const {
        for (std::size_t i = 0; i < size_; ++i) {
            cb(buf_[i]);
        }
    }

    /// @brief Removes events older than @p threshold_us from the top.
    /// Keeps only events with t >= top_time - threshold_us.
    void trim_old(Metavision::timestamp threshold_us) {
        if (size_ == 0) return;
        const Metavision::timestamp newest = buf_[size_ - 1].t;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < size_; ++i) {
            if (buf_[i].t + threshold_us >= newest) {
                buf_[kept++] = buf_[i];
            }
        }
        size_ = kept;
    }

    void clear() { size_ = 0; }
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

private:
    const std::size_t capacity_;
    std::vector<Event> buf_;
    std::size_t size_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_LIFO_EVENT_BUFFER_H
