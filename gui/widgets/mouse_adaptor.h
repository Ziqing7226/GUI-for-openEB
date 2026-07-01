// gui/widgets/mouse_adaptor.h — mouse → sensor coordinate conversion.
//
// Design §1.6.8 (jAER EventFilter2DMouseAdaptor). A utility QObject that
// converts Qt widget mouse coordinates into sensor pixel coordinates,
// accounting for the letterboxed / scaled display rect inside the host
// widget. Emits clicked(int x, int y, QMouseEvent*) with sensor pixel
// coordinates when the user clicks inside the displayed image area.
//
// The host display widget sets the sensor geometry and the image rect (the
// sub-rectangle of the widget where the camera image is actually drawn),
// then forwards mouse events (or installs this as an event filter).

#ifndef GUI_WIDGETS_MOUSE_ADAPTOR_H
#define GUI_WIDGETS_MOUSE_ADAPTOR_H

#include <QObject>
#include <QPoint>
#include <QRect>

class QMouseEvent;
class QWidget;

namespace gui {

class MouseAdaptor : public QObject {
    Q_OBJECT
public:
    explicit MouseAdaptor(QObject* parent = nullptr);

    /// @brief Sets the sensor dimensions in pixels.
    void set_sensor_size(int width, int height);
    int sensor_width() const { return sensor_w_; }
    int sensor_height() const { return sensor_h_; }

    /// @brief Sets the rectangle (in widget coordinates) where the camera
    /// image is drawn (the letterbox). Pass an invalid rect to map over the
    /// entire host widget area.
    void set_image_rect(const QRect& rect);
    QRect image_rect() const { return image_rect_; }

    /// @brief Converts a widget-pixel point to sensor-pixel coordinates.
    /// @return true if the point falls inside the sensor area.
    bool map_to_sensor(const QPoint& widget_pos, QPoint& sensor_pos) const;

    /// @brief Convenience: converts a sensor-pixel point back to widget
    /// coordinates (useful for drawing overlays aligned with sensor coords).
    QPoint map_to_widget(const QPoint& sensor_pos) const;

    /// @brief Forwards a mouse press event: converts the position and emits
    /// clicked() if it lands inside the sensor area.
    void handle_mouse_press(QMouseEvent* event);

    /// @brief Installs this adaptor as an event filter on @p host so that
    /// mouse presses are auto-converted. The host retains ownership.
    void attach(QWidget* host);

    /// @brief Removes a previously installed event filter.
    void detach(QWidget* host);

signals:
    /// @brief Emitted on a click inside the sensor area.
    /// @param x Sensor column.
    /// @param y Sensor row.
    /// @param event The original QMouseEvent (for modifiers/buttons).
    void clicked(int x, int y, QMouseEvent* event);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int sensor_w_{0};
    int sensor_h_{0};
    QRect image_rect_{};  // invalid → use host widget rect
};

} // namespace gui

#endif // GUI_WIDGETS_MOUSE_ADAPTOR_H
