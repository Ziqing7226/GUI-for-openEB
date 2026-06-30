// gui/app/frame_pipeline.h — Qt-facing wrapper around gui_algo::FrameGenerator.
//
// Lives on the GUI thread. The frame callback (invoked on the CDFrameGenerator
// internal thread) deep-copies the cv::Mat into a QImage and emits frame_ready
// via a queued signal so the GUI thread can safely consume it.

#ifndef GUI_APP_FRAME_PIPELINE_H
#define GUI_APP_FRAME_PIPELINE_H

#include <QObject>
#include <QImage>
#include <cstdint>
#include <memory>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/frame_generator.h"

namespace gui {

class FramePipeline : public QObject {
    Q_OBJECT
public:
    explicit FramePipeline(QObject* parent = nullptr);
    ~FramePipeline();

    /// @brief Starts the pipeline. Returns false if already running or invalid.
    bool start(long width, long height,
               std::uint16_t fps,
               Metavision::timestamp accumulation_time_us);
    void stop();
    bool is_running() const { return window_id_ >= 0; }

    /// @brief Thread-safe: called from the SDK CD callback.
    void add_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

    void set_accumulation_time_us(Metavision::timestamp us);
    void set_color_palette(Metavision::ColorPalette palette);

signals:
    void frame_ready(QImage frame, Metavision::timestamp ts);

private:
    std::unique_ptr<gui_algo::FrameGenerator> generator_;
    int window_id_{-1};
};

} // namespace gui

#endif // GUI_APP_FRAME_PIPELINE_H
