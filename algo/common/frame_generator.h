// algo/common/frame_generator.h — multi-window event frame generator.
//
// Wraps Metavision::CDFrameGenerator. A single shared event feed is fanned out
// to N independent accumulation windows, each with its own FPS, accumulation
// time and output callback. This supports the multi-window display layout
// described in design.md §5.6.

#ifndef GUI_ALGO_COMMON_FRAME_GENERATOR_H
#define GUI_ALGO_COMMON_FRAME_GENERATOR_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>
#include <metavision/sdk/core/utils/colors.h> // ColorPalette
#include <metavision/sdk/base/events/event_cd.h>

namespace gui_algo {

/// @brief Multi-window frame generator built on Metavision::CDFrameGenerator.
class FrameGenerator {
public:
    using FrameCallback = Metavision::PeriodicFrameGenerationAlgorithm::OutputCb;

    struct WindowParams {
        std::uint16_t fps{30};
        Metavision::timestamp accumulation_time_us{33000};
    };

    explicit FrameGenerator(long width, long height);
    ~FrameGenerator();

    FrameGenerator(const FrameGenerator&) = delete;
    FrameGenerator& operator=(const FrameGenerator&) = delete;

    /// @brief Adds a named accumulation window.
    /// @return Window id >= 0 on success, -1 on failure.
    int add_window(const std::string& name, const WindowParams& params, const FrameCallback& cb);

    bool remove_window(int id);
    void clear_windows();

    /// @brief Thread-safe: feeds events to every active window.
    void add_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

    void set_color_palette(const Metavision::ColorPalette& palette);

    /// @brief Updates the accumulation time for all windows.
    void set_accumulation_time_us(Metavision::timestamp us);

    long width() const { return width_; }
    long height() const { return height_; }

private:
    struct Window {
        int id;
        std::string name;
        std::unique_ptr<Metavision::CDFrameGenerator> generator;
    };

    long width_;
    long height_;
    Metavision::ColorPalette palette_;
    std::mutex mutex_;
    std::map<int, Window> windows_;
    int next_id_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_FRAME_GENERATOR_H
