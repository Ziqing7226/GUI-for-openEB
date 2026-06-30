// algo/common/frame_generator.cpp

#include "frame_generator.h"

namespace gui_algo {

FrameGenerator::FrameGenerator(long width, long height)
    : width_(width), height_(height), palette_(Metavision::ColorPalette::Dark) {}

FrameGenerator::~FrameGenerator() {
    clear_windows();
}

int FrameGenerator::add_window(const std::string& name,
                               const WindowParams& params,
                               const FrameCallback& cb) {
    if (!cb) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Window w;
    w.id = next_id_++;
    w.name = name;
    w.generator = std::make_unique<Metavision::CDFrameGenerator>(width_, height_, false);
    w.generator->set_color_palette(palette_);
    w.generator->set_display_accumulation_time_us(params.accumulation_time_us);
    if (!w.generator->start(params.fps, cb)) {
        return -1;
    }
    int id = w.id;
    windows_.emplace(id, std::move(w));
    return id;
}

bool FrameGenerator::remove_window(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = windows_.find(id);
    if (it == windows_.end()) {
        return false;
    }
    it->second.generator->stop();
    windows_.erase(it);
    return true;
}

void FrameGenerator::clear_windows() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : windows_) {
        if (kv.second.generator) {
            kv.second.generator->stop();
        }
    }
    windows_.clear();
}

void FrameGenerator::add_events(const Metavision::EventCD* begin,
                                const Metavision::EventCD* end) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : windows_) {
        if (kv.second.generator) {
            kv.second.generator->add_events(begin, end);
        }
    }
}

void FrameGenerator::set_color_palette(const Metavision::ColorPalette& palette) {
    std::lock_guard<std::mutex> lock(mutex_);
    palette_ = palette;
    for (auto& kv : windows_) {
        if (kv.second.generator) {
            kv.second.generator->set_color_palette(palette);
        }
    }
}

} // namespace gui_algo
