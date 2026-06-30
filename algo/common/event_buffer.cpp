// algo/common/event_buffer.cpp — explicit instantiation for the CD event type.

#include "event_buffer.h"

#include <metavision/sdk/base/events/event_cd.h>

namespace gui_algo {

// Ensures the template is instantiated in this translation unit so that
// downstream linkers see the symbol for the most common event type.
template class EventRingBuffer<Metavision::EventCD>;

} // namespace gui_algo
