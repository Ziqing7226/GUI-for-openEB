// gui/app/camera_controller.cpp

#include "camera_controller.h"

#include <QMetaObject>
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
    return connect_file_speed(path, 1.0);
}

bool CameraController::connect_file_speed(const std::string& path, double speed) {
    teardown();
    try {
        Metavision::FileConfigHints hints;
        // speed == 0 → as-fast-as-possible (real_time_playback disabled);
        // otherwise real-time playback.
        hints.real_time_playback(speed > 0.0);
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
        // Don't emit started() here: the status-change callback fires it
        // exactly once when the SDK confirms the STARTED transition.
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
        // Don't emit stopped() here: the status-change callback fires it
        // exactly once when the SDK confirms the STOPPED transition. For
        // runtime errors (file EOF, disconnect), the error callback also
        // emits stopped() so the UI is notified even if the status callback
        // never fires.
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

bool CameraController::is_running() const {
    return camera_ && camera_->is_running();
}

Metavision::timestamp CameraController::last_timestamp_us() const {
    return last_ts_.load(std::memory_order_relaxed);
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
            // Calling camera_->stop() from within this callback can deadlock
            // (the SDK dispatches this on the same thread that stop() would
            // join). Defer to the GUI thread.
            QMetaObject::invokeMethod(this, [this]() {
                if (!camera_) return;  // already disconnected via teardown()
                if (camera_->is_running()) {
                    try { camera_->stop(); } catch (...) {}
                }
                emit stopped();
            }, Qt::QueuedConnection);
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
    // The SDK dispatches this callback on its streaming/decoding thread with
    // NO try/catch, so an exception escaping the lambda would call
    // std::terminate and crash the whole GUI with no diagnostic. The filter
    // chain allocates a copy of every batch (and each OpenEB algorithm may
    // reallocate its output), so std::bad_alloc at high event rates is
    // plausible. Wrap the body and surface failures to the GUI thread.
    cd_cb_id_ = camera_->cd().add_callback(
        [this](const Metavision::EventCD* b, const Metavision::EventCD* e) {
            try {
                statistics_.add_events(b, e);
                if (b != e) {
                    last_ts_.store((e - 1)->t, std::memory_order_relaxed);
                }
                if (filter_chain_.has_enabled()) {
                    std::vector<Metavision::EventCD> filtered;
                    filter_chain_.process(b, e, filtered);
                    if (!filtered.empty()) {
                        frame_pipeline_.add_events(filtered.data(),
                                                   filtered.data() + filtered.size());
                    }
                } else {
                    frame_pipeline_.add_events(b, e);
                }
            } catch (const std::exception& ex) {
                QMetaObject::invokeMethod(this, [this, msg = std::string(ex.what())]() {
                    emit runtime_warning(QString::fromUtf8(msg.c_str()));
                }, Qt::QueuedConnection);
            } catch (...) {
                // Swallow to keep the stream alive; the SDK thread must not
                // propagate exceptions out of the callback.
            }
        });

    statistics_.reset();
    last_ts_.store(-1, std::memory_order_relaxed);
    filter_chain_.set_geometry(sensor_info_.width, sensor_info_.height);

    // Start the frame pipeline for the new sensor geometry.
    const long w = sensor_info_.width;
    const long h = sensor_info_.height;
    if (!frame_pipeline_.start(w, h, /*fps*/ 30, /*accumulation_us*/ 33000)) {
        emit runtime_warning(tr("Failed to start frame pipeline."));
    }

    emit connected(sensor_info_);
}

void CameraController::teardown() {
    // 1. Remove the SDK callbacks FIRST so the SDK thread stops calling into
    //    FramePipeline / FilterChain / StatisticsController. Without this,
    //    stopping the pipeline (which resets generator_) races with the CD
    //    callback's frame_pipeline_.add_events() — a use-after-free.
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

    // 2. Now that no SDK thread can touch it, stop the frame pipeline.
    frame_pipeline_.stop();

    sensor_info_ = SensorInfo{};
    is_file_ = false;
    last_ts_.store(-1, std::memory_order_relaxed);
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
