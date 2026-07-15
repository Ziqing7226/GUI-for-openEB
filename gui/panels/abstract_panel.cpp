// gui/panels/abstract_panel.cpp

#include "abstract_panel.h"

#include <QStyle>

#include "app/camera_controller.h"

namespace gui {

void AbstractPanel::restyle(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void AbstractPanel::bind_camera(CameraController* cam) {
    if (camera_) {
        disconnect(camera_, nullptr, this, nullptr);
    }
    camera_ = cam;
    if (camera_) {
        // Forward connected/disconnected to the panel's virtual slots.
        // The SensorInfo from `connected` is available via camera_->sensor_info()
        // (already updated before the signal is emitted), so the slot can read
        // it directly without needing the parameter.
        connect(camera_, &CameraController::connected, this, [this](const SensorInfo&) {
            on_camera_connected(camera_);
        });
        connect(camera_, &CameraController::disconnected, this, [this]() {
            on_camera_disconnected();
        });
    }
}

} // namespace gui
