// gui/recorder/playback_controller.cpp

#include "playback_controller.h"

#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/sdk/stream/file_config_hints.h>
#include <metavision/sdk/stream/offline_streaming_control.h>

#include "app/camera_controller.h"

namespace gui {

PlaybackController::PlaybackController(QObject* parent) : QObject(parent) {
    probe_timer_.setInterval(100);
    connect(&probe_timer_, &QTimer::timeout, this, &PlaybackController::probe_position);
}

void PlaybackController::set_camera(CameraController* controller) {
    if (controller_ == controller) return;
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    // Reset stale playback state from the previous controller before binding
    // the new one: path_/duration_us_ belong to a different stream and
    // playing_ no longer reflects reality.
    probe_timer_.stop();
    path_.clear();
    duration_us_ = 0;
    if (playing_) {
        playing_ = false;
        emit state_changed(false);
    }
    controller_ = controller;
    if (controller_) {
        // When a file reaches EOF the SDK defers camera_->stop() to the GUI
        // thread and emits CameraController::stopped. Without this connection
        // playing_ would stay true, the probe timer would keep firing, and
        // the play button would keep showing "Pause" — the user would have
        // to click twice to restart (once to "pause" an already-stopped
        // stream, once to play). Loop mode handles its own restart in
        // probe_position(), so it is exempt.
        connect(controller_, &CameraController::stopped, this, [this]() {
            if (loop_ && playing_) return; // loop restart handled in probe_position
            if (playing_) {
                playing_ = false;
                probe_timer_.stop();
                emit state_changed(false);
            }
        });
    }
}

bool PlaybackController::available() const {
    return controller_ && controller_->is_connected() && controller_->is_file_source();
}

bool PlaybackController::open_file(const QString& path, double speed) {
    if (!controller_) {
        emit error(tr("No camera controller bound."));
        return false;
    }
    path_ = path;
    speed_ = speed;
    if (!controller_->connect_file_speed(path.toStdString(), speed)) {
        return false;
    }
    duration_us_ = query_duration();
    playing_ = false;
    if (!controller_->start()) {
        // start() already emitted an error via CameraController.
        return false;
    }
    playing_ = true;
    probe_timer_.start();
    emit opened(duration_us_);
    emit state_changed(playing_);
    emit speed_changed(speed_);
    return true;
}

void PlaybackController::play() {
    if (!available() || playing_) {
        return;
    }
    if (!controller_->start()) {
        // start() already emitted an error via CameraController.
        return;
    }
    playing_ = true;
    probe_timer_.start();
    emit state_changed(true);
}

void PlaybackController::pause() {
    if (!playing_) {
        return;
    }
    controller_->stop();
    playing_ = false;
    probe_timer_.stop();
    emit state_changed(false);
}

void PlaybackController::toggle_play_pause() {
    if (playing_) {
        pause();
    } else {
        play();
    }
}

bool PlaybackController::seek(Metavision::timestamp t_us) {
    if (!available()) {
        return false;
    }
    auto* cam = controller_->camera_handle();
    if (!cam) {
        return false;
    }
    try {
        auto& osc = cam->offline_streaming_control();
        if (!osc.is_ready()) {
            emit error(tr("Seeking is not available for this file."));
            return false;
        }
        if (!osc.seek(t_us)) {
            return false;
        }
        emit position_changed(t_us, duration_us_);
        return true;
    } catch (const Metavision::CameraException& e) {
        emit error(QString::fromUtf8(e.what()));
        return false;
    }
}

void PlaybackController::set_loop(bool on) {
    loop_ = on;
    emit loop_changed(on);
}

void PlaybackController::set_speed(double speed) {
    if (!available() || path_.isEmpty()) {
        speed_ = speed;
        return;
    }
    if (speed == speed_) {
        return;
    }
    // Reopen the file at the new speed, preserving the current position.
    // Preserve the prior play/pause state: changing the speed combo should
    // not silently resume a paused stream. Roll back speed_ on failure so
    // the UI does not display a speed that was never applied.
    const bool was_playing = playing_;
    const Metavision::timestamp pos = query_position();
    const double old_speed = speed_;
    speed_ = speed;
    if (!controller_->connect_file_speed(path_.toStdString(), speed)) {
        speed_ = old_speed;
        emit error(tr("Failed to change playback speed."));
        return;
    }
    duration_us_ = query_duration();
    if (pos > 0) {
        seek(pos);
    }
    if (was_playing) {
        if (!controller_->start()) {
            speed_ = old_speed;
            playing_ = false;
            emit state_changed(false);
            emit speed_changed(old_speed);
            return;
        }
        probe_timer_.start();
    }
    playing_ = was_playing;
    emit speed_changed(speed_);
    emit state_changed(was_playing);
}

Metavision::timestamp PlaybackController::duration_us() const {
    return duration_us_;
}

Metavision::timestamp PlaybackController::position_us() const {
    return query_position();
}

void PlaybackController::probe_position() {
    const Metavision::timestamp pos = query_position();
    if (pos >= 0) {
        emit position_changed(pos, duration_us_);
    }
    // Loop-on-EOF: when the controller reports the stream has stopped due to
    // EOF, restart from the beginning if looping is enabled. Break out of the
    // loop on failure so we don't retry every 100 ms forever with no backoff.
    if (loop_ && available() && !controller_->is_running() && playing_) {
        if (!seek(0) || !controller_->start()) {
            playing_ = false;
            probe_timer_.stop();
            emit error(tr("Loop restart failed; stopping playback."));
            emit state_changed(false);
        }
    }
}

Metavision::timestamp PlaybackController::query_duration() const {
    if (!available()) {
        return 0;
    }
    auto* cam = controller_->camera_handle();
    if (!cam) {
        return 0;
    }
    try {
        auto& osc = cam->offline_streaming_control();
        if (!osc.is_ready()) {
            return 0;
        }
        return osc.get_duration();
    } catch (...) {
        return 0;
    }
}

Metavision::timestamp PlaybackController::query_position() const {
    if (!available()) {
        return 0;
    }
    return controller_->last_timestamp_us();
}

} // namespace gui
