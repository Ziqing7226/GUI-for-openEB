// gui/app/frame_pipeline.cpp

#include "frame_pipeline.h"

#include <opencv2/imgproc.hpp>

namespace gui {

FramePipeline::FramePipeline(QObject* parent) : QObject(parent) {}

FramePipeline::~FramePipeline() {
    stop();
}

bool FramePipeline::start(long width, long height,
                          std::uint16_t fps,
                          Metavision::timestamp accumulation_time_us) {
    if (is_running()) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    generator_ = std::make_unique<gui_algo::FrameGenerator>(width, height);

    gui_algo::FrameGenerator::WindowParams params;
    params.fps = fps;
    params.accumulation_time_us = accumulation_time_us;

    // The callback runs on CDFrameGenerator's internal thread. We must deep
    // copy the cv::Mat before emitting, since the SDK reuses it.
    window_id_ = generator_->add_window(
        "main", params,
        [this](Metavision::timestamp ts, cv::Mat& frame) {
            if (frame.empty()) {
                return;
            }
            cv::Mat rgb;
            if (frame.channels() == 1) {
                cv::cvtColor(frame, rgb, cv::COLOR_GRAY2RGB);
            } else {
                cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
            }
            QImage img(rgb.data, rgb.cols, rgb.rows,
                       static_cast<int>(rgb.step), QImage::Format_RGB888);
            QImage copy = img.copy(); // deep copy (rgb is local)
            emit frame_ready(std::move(copy), ts);
        });

    if (window_id_ < 0) {
        generator_.reset();
        return false;
    }
    return true;
}

void FramePipeline::stop() {
    if (generator_) {
        generator_->remove_window(window_id_);
        generator_.reset();
    }
    window_id_ = -1;
}

void FramePipeline::add_events(const Metavision::EventCD* begin,
                               const Metavision::EventCD* end) {
    if (generator_) {
        generator_->add_events(begin, end);
    }
}

void FramePipeline::set_accumulation_time_us(Metavision::timestamp /*us*/) {
    // Phase 1: accumulation time is set at window creation. Live update is
    // added together with the full Display panel parameter wiring.
    // (CDFrameGenerator supports set_display_accumulation_time_us; re-creating
    // the window with new params is the simplest robust path for MVP.)
}

void FramePipeline::set_color_palette(Metavision::ColorPalette palette) {
    if (generator_) {
        generator_->set_color_palette(palette);
    }
}

} // namespace gui
