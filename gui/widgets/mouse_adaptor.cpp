// gui/widgets/mouse_adaptor.cpp

#include "mouse_adaptor.h"

#include <QEvent>
#include <QMouseEvent>
#include <QWidget>

namespace gui {

MouseAdaptor::MouseAdaptor(QObject* parent) : QObject(parent) {}

void MouseAdaptor::set_sensor_size(int width, int height) {
    sensor_w_ = (width > 0) ? width : 0;
    sensor_h_ = (height > 0) ? height : 0;
}

void MouseAdaptor::set_image_rect(const QRect& rect) {
    image_rect_ = rect;
}

bool MouseAdaptor::map_to_sensor(const QPoint& widget_pos, QPoint& sensor_pos) const {
    if (sensor_w_ <= 0 || sensor_h_ <= 0) return false;

    // Resolve the active drawing rect: explicit image_rect_ if valid,
    // otherwise fall back to the full host widget rect (when attached).
    QRect rect = image_rect_;
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
        return false;
    }
    if (!rect.contains(widget_pos)) return false;

    // Map widget px → sensor px with rounding, clamped to sensor bounds.
    const double sx = static_cast<double>(widget_pos.x() - rect.x()) / rect.width();
    const double sy = static_cast<double>(widget_pos.y() - rect.y()) / rect.height();
    int x = static_cast<int>(sx * sensor_w_);
    int y = static_cast<int>(sy * sensor_h_);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= sensor_w_) x = sensor_w_ - 1;
    if (y >= sensor_h_) y = sensor_h_ - 1;
    sensor_pos = QPoint(x, y);
    return true;
}

QPoint MouseAdaptor::map_to_widget(const QPoint& sensor_pos) const {
    QRect rect = image_rect_;
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0 ||
        sensor_w_ <= 0 || sensor_h_ <= 0) {
        return QPoint();
    }
    const double fx = static_cast<double>(sensor_pos.x()) / sensor_w_;
    const double fy = static_cast<double>(sensor_pos.y()) / sensor_h_;
    return QPoint(rect.x() + static_cast<int>(fx * rect.width()),
                  rect.y() + static_cast<int>(fy * rect.height()));
}

void MouseAdaptor::handle_mouse_press(QMouseEvent* event) {
    if (event == nullptr) return;
    if (event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonDblClick) {
        return;
    }
    QPoint sensor;
    if (map_to_sensor(event->position().toPoint(), sensor)) {
        emit clicked(sensor.x(), sensor.y(), event);
    }
}

void MouseAdaptor::attach(QWidget* host) {
    if (host != nullptr) {
        host->installEventFilter(this);
    }
}

void MouseAdaptor::detach(QWidget* host) {
    if (host != nullptr) {
        host->removeEventFilter(this);
    }
}

bool MouseAdaptor::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonDblClick) {
        auto* me = static_cast<QMouseEvent*>(event);
        QPoint sensor;
        if (map_to_sensor(me->position().toPoint(), sensor)) {
            emit clicked(sensor.x(), sensor.y(), me);
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace gui
