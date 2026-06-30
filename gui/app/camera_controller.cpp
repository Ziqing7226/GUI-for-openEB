// gui/app/camera_controller.cpp

#include "camera_controller.h"

#include <QString>

#include <metavision/sdk/stream/camera_error_code.h>
#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/sdk/stream/file_config_hints.h>

namespace gui {

CameraController::CameraController(QObject* parent)
    : QObject(parent), frame_pipeline_(nullptr), statistics_(nullptr) {}

CameraController::~CameraController() {
    teardown();
}

std::vector<std::pair<QString, QString>> CameraController::list_online_sources() {
    std::vector<std::pair<QString, QString>> out;
    try {
        const auto sources = Metavision::Camera::list_online_sources();
        for (const auto& kv : sources) {
            QString type_label;
            switch (kv.first) {
                case Metavision::OnlineSourceType::EMBEDDED: type_label = "Embedded"; break;
                case Metavision::OnlineSourceType::USB:      type_label = "USB"; break;
                case Metavision::OnlineSourceType::REMOTE:   type_label = "Remote"; break;
                default:                                     type_label = "Other"; break;
            }
            for (const auto& serial : kv.second) {
                out.emplace_back(type_label, QString::fromStdString(serial));
            }
        }
    } catch (const Metavision::CameraException&) {
        // ignore — return empty list
    }
    return out;
}

bool CameraController::connect_first_available() {
    teardown();
    try {
        auto cam = Metavision::Camera::from_first_available();
        setup_camera(std::move(cam), false);
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

bool CameraController::connect_serial(const std::string& serial) {
    teardown();
    try {
        auto cam = Metavision::Camera::from_serial(serial);
        setup_camera(std::move(cam), false);
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

bool CameraController::connect_file(const std::string& path) {
    teardown();
    try {
        Metavision::FileConfigHints hints;
        hints.real_time_playback(true);
        auto cam = Metavision::Camera::from_file(path, hints);
        setup_camera(std::move(cam), true);
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

void CameraController::disconnect() {
    teardown();
    emit disconnected();
}

bool CameraController::start() {
    if (!camera_) {
        return false;
    }
    try {
        if (!camera_->is_running()) {
            camera_->start();
        }
        emit started();
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

bool CameraController::stop() {
    if (!camera_) {
        return false;
    }
    try {
        if (camera_->is_running()) {
            camera_->stop();
        }
        emit stopped();
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

bool CameraController::is_running() const {
    return camera_ && camera_->is_running();
}

bool CameraController::save_config(const std::string& path) {
    if (!camera_) {
        return false;
    }
    try {
        return camera_->save(path);
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

bool CameraController::load_config(const std::string& path) {
    if (!camera_) {
        return false;
    }
    try {
        return camera_->load(path);
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

// ---------------------------------------------------------------------------
// Phase 2 facility accessors
// ---------------------------------------------------------------------------
// All go through Device::get_facility<T>() which returns a nullable pointer
// (vs Camera::get_facility<T>() which throws on unsupported features). This
// lets the GUI degrade gracefully by disabling the corresponding panel.
facility::Biases* CameraController::biases_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::Biases>();
}
facility::Roi* CameraController::roi_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::Roi>();
}
facility::AntiFlicker* CameraController::anti_flicker_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::AntiFlicker>();
}
facility::TrailFilter* CameraController::trail_filter_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::TrailFilter>();
}
facility::Erc* CameraController::erc_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::Erc>();
}
facility::TriggerIn* CameraController::trigger_in_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::TriggerIn>();
}
facility::TriggerOut* CameraController::trigger_out_facility() {
    if (!camera_) return nullptr;
    return camera_->get_device().get_facility<facility::TriggerOut>();
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

void CameraController::setup_camera(Metavision::Camera&& cam, bool is_file) {
    is_file_ = is_file;
    camera_ = std::make_unique<Metavision::Camera>(std::move(cam));
    fetch_sensor_info();

    // Runtime error callback: file EOF, disconnects, firmware errors arrive here.
    err_cb_id_ = camera_->add_runtime_error_callback(
        [this](const Metavision::CameraException& e) {
            // Reaching end-of-file is a normal stop condition for playback.
            const QString msg = QString::fromUtf8(e.what());
            if (is_file_) {
                emit runtime_warning(tr("Playback ended: %1").arg(msg));
            } else {
                emit error(msg);
            }
            if (camera_ && camera_->is_running()) {
                try { camera_->stop(); } catch (...) {}
            }
            emit stopped();
        });

    // Status change callback.
    status_cb_id_ = camera_->add_status_change_callback(
        [this](const Metavision::CameraStatus& status) {
            if (status == Metavision::CameraStatus::STARTED) {
                emit started();
            } else {
                emit stopped();
            }
        });

    // CD callback: forward events to the frame pipeline + statistics.
    cd_cb_id_ = camera_->cd().add_callback(
        [this](const Metavision::EventCD* b, const Metavision::EventCD* e) {
            frame_pipeline_.add_events(b, e);
            statistics_.add_events(b, e);
        });

    statistics_.reset();

    // Start the frame pipeline for the new sensor geometry.
    const long w = sensor_info_.width;
    const long h = sensor_info_.height;
    if (!frame_pipeline_.start(w, h, /*fps*/ 30, /*accumulation_us*/ 33000)) {
        emit runtime_warning(tr("Failed to start frame pipeline."));
    }

    emit connected(sensor_info_);
}

void CameraController::teardown() {
    // 1. Stop the frame pipeline first (so it stops pulling from the SDK).
    frame_pipeline_.stop();

    // 2. Remove callbacks before destroying the camera.
    if (camera_) {
        if (cd_cb_id_) {
            camera_->cd().remove_callback(*cd_cb_id_);
            cd_cb_id_.reset();
        }
        if (err_cb_id_) {
            camera_->remove_runtime_error_callback(*err_cb_id_);
            err_cb_id_.reset();
        }
        if (status_cb_id_) {
            camera_->remove_status_change_callback(*status_cb_id_);
            status_cb_id_.reset();
        }
        if (camera_->is_running()) {
            try { camera_->stop(); } catch (...) {}
        }
        camera_.reset();
    }
    sensor_info_ = SensorInfo{};
    is_file_ = false;
}

void CameraController::fetch_sensor_info() {
    SensorInfo info;
    info.is_file = is_file_;
    if (!camera_) {
        sensor_info_ = info;
        return;
    }
    try {
        const auto& g = camera_->geometry();
        info.width = g.get_width();
        info.height = g.get_height();
    } catch (...) {}
    try {
        const auto& cfg = camera_->get_camera_configuration();
        info.serial = QString::fromStdString(cfg.serial_number);
        info.integrator = QString::fromStdString(cfg.integrator);
        info.plugin_name = QString::fromStdString(cfg.plugin_name);
        info.encoding_format = QString::fromStdString(cfg.data_encoding_format);
        info.firmware_version = QString::fromStdString(cfg.firmware_version);
    } catch (...) {}
    try {
        const auto& gen = camera_->generation();
        info.generation_name = QString::fromStdString(gen.name());
        info.generation_major = gen.version_major();
        info.generation_minor = gen.version_minor();
    } catch (...) {}
    sensor_info_ = info;
}

} // namespace gui
