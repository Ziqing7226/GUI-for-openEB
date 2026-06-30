// algo/common/data_loader.cpp

#include "data_loader.h"

#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/sdk/stream/offline_streaming_control.h>

namespace gui_algo {

DataLoader::DataLoader() = default;

DataLoader::~DataLoader() {
    close();
}

bool DataLoader::open(const std::string& path) {
    close();
    try {
        Metavision::FileConfigHints hints;
        hints.real_time_playback(true);
        auto cam = Metavision::Camera::from_file(path, hints);
        const auto& geometry = cam.geometry();
        width_ = geometry.get_width();
        height_ = geometry.get_height();
        const auto& cfg = cam.get_camera_configuration();
        serial_ = cfg.serial_number;
        integrator_ = cfg.integrator;
        plugin_ = cfg.plugin_name;
        camera_ = std::make_unique<Metavision::Camera>(std::move(cam));
        return true;
    } catch (const Metavision::CameraException& e) {
        camera_.reset();
        return false;
    }
}

void DataLoader::close() {
    if (cd_cb_id_) {
        if (camera_) {
            camera_->cd().remove_callback(*cd_cb_id_);
        }
        cd_cb_id_.reset();
    }
    if (camera_) {
        if (camera_->is_running()) {
            camera_->stop();
        }
        camera_.reset();
    }
    width_ = 0;
    height_ = 0;
    serial_.clear();
    integrator_.clear();
    plugin_.clear();
}

std::optional<Metavision::CallbackId> DataLoader::set_event_callback(const EventCallback& cb) {
    if (!camera_ || !cb) {
        return std::nullopt;
    }
    clear_event_callback();
    Metavision::CallbackId id = camera_->cd().add_callback(cb);
    cd_cb_id_ = id;
    return id;
}

void DataLoader::clear_event_callback() {
    if (cd_cb_id_ && camera_) {
        camera_->cd().remove_callback(*cd_cb_id_);
    }
    cd_cb_id_.reset();
}

bool DataLoader::start() {
    if (!camera_) {
        return false;
    }
    try {
        return camera_->start();
    } catch (const Metavision::CameraException&) {
        return false;
    }
}

bool DataLoader::stop() {
    if (!camera_) {
        return false;
    }
    try {
        return camera_->stop();
    } catch (const Metavision::CameraException&) {
        return false;
    }
}

bool DataLoader::is_running() const {
    return camera_ && camera_->is_running();
}

Metavision::timestamp DataLoader::duration() const {
    if (!camera_ || !camera_->offline_streaming_control().is_ready()) {
        return 0;
    }
    return camera_->offline_streaming_control().get_duration();
}

Metavision::timestamp DataLoader::last_timestamp() const {
    if (!camera_) {
        return 0;
    }
    try {
        return camera_->get_last_timestamp();
    } catch (const Metavision::CameraException&) {
        return 0;
    }
}

bool DataLoader::seek(Metavision::timestamp t) {
    if (!camera_ || !camera_->offline_streaming_control().is_ready()) {
        return false;
    }
    return camera_->offline_streaming_control().seek(t);
}

bool DataLoader::seekable() const {
    return camera_ && camera_->offline_streaming_control().is_ready();
}

} // namespace gui_algo
