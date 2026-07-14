// algo/common/event_buffer.h — high-performance event ring buffer.
//
// Lock-free single-producer / single-consumer ring buffer for events.
// Cache-line padded head/tail to avoid false sharing. Used to decouple the
// SDK data thread (producer) from display / processing consumers.

#ifndef GUI_ALGO_COMMON_EVENT_BUFFER_H
#define GUI_ALGO_COMMON_EVENT_BUFFER_H

#include <atomic>
#include <cstddef>
#include <vector>

namespace gui_algo {

constexpr std::size_t kCacheLineSize = 64;

/// @brief Lock-free SPSC ring buffer for events.
///
/// Capacity is fixed at construction. The producer calls push() from one
/// thread, the consumer calls pull() from another. push() never blocks: if the
/// buffer is full, the oldest events are effectively overwritten only if the
/// caller chooses to; by default push() drops new events when full (returns the
/// number actually accepted).
template <typename EventT>
class EventRingBuffer {
public:
    explicit EventRingBuffer(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity), buffer_(capacity_),
          head_(0), tail_(0) {}

    /// Push up to @p n events. Returns the number actually written (may be < n
    /// when the buffer is full).
    std::size_t push(const EventT* events, std::size_t n) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        const std::size_t used = h - t;
        const std::size_t free_slots = capacity_ - used;
        const std::size_t to_push = n < free_slots ? n : free_slots;
        for (std::size_t i = 0; i < to_push; ++i) {
            buffer_[(h + i) % capacity_] = events[i];
        }
        head_.store(h + to_push, std::memory_order_release);
        return to_push;
    }

    /// Pull up to @p n events into @p out. Returns the number actually read.
    std::size_t pull(EventT* out, std::size_t n) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t available = h - t;
        const std::size_t to_pull = n < available ? n : available;
        for (std::size_t i = 0; i < to_pull; ++i) {
            out[i] = buffer_[(t + i) % capacity_];
        }
        tail_.store(t + to_pull, std::memory_order_release);
        return to_pull;
    }

    std::size_t size() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const { return capacity_; }

    void clear() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    const std::size_t capacity_;
    std::vector<EventT> buffer_;

    // Padded to avoid false sharing between producer and consumer.
    alignas(kCacheLineSize) std::atomic<std::size_t> head_;
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_EVENT_BUFFER_H
