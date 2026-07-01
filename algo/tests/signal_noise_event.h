// algo/tests/signal_noise_event.h — signal/noise annotated event type.
//
// Phase 10 (design §4.6.2): extends Event with an is_signal flag so the
// NoiseTester framework (§4.6.1) can score filter output against ground truth.
// Used only in tests; production code uses the plain Event POD.

#ifndef GUI_ALGO_TESTS_SIGNAL_NOISE_EVENT_H
#define GUI_ALGO_TESTS_SIGNAL_NOISE_EVENT_H

#include "algo/common/event.h"

namespace gui_algo {

struct SignalNoiseEvent : public Event {
    bool is_signal{false};

    SignalNoiseEvent() = default;
    SignalNoiseEvent(const Event& e, bool signal) : Event(e), is_signal(signal) {}
    SignalNoiseEvent(uint16_t x_, uint16_t y_, short p_, Metavision::timestamp t_, bool signal)
        : Event(x_, y_, p_, t_), is_signal(signal) {}
};

} // namespace gui_algo

#endif // GUI_ALGO_TESTS_SIGNAL_NOISE_EVENT_H
