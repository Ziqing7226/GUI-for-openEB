// gui/display/event_display_widget.h — OpenGL-accelerated event frame display.
//
// A QOpenGLWidget that uploads the latest event frame (received as a QImage)
// to a texture and renders it as a letterboxed textured quad via a small
// GLSL shader. A centered overlay label shows a "no signal" hint when no
// camera is connected.

#ifndef GUI_DISPLAY_EVENT_DISPLAY_WIDGET_H
#define GUI_DISPLAY_EVENT_DISPLAY_WIDGET_H

#include <QImage>
#include <QLabel>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRect>
#include <memory>

namespace gui {

class EventDisplayWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit EventDisplayWidget(QWidget* parent = nullptr);
    ~EventDisplayWidget();

public slots:
    /// @brief Sets the latest frame and schedules a repaint.
    /// Thread-safe: the QImage is a deep copy owned by the caller.
    void set_frame(const QImage& frame);

    /// @brief Clears the display (e.g. on disconnect).
    void clear();

    /// @brief Toggles ROI drag mode. When on, the cursor becomes a crosshair
    /// and mouse drag draws a rectangle; on release the roi_dragged signal
    /// fires with sensor-pixel coordinates.
    void set_roi_drag_mode(bool on);

    /// @brief Paints a persistent ROI overlay rectangle (in sensor pixel
    /// coordinates). Pass enabled=false to hide it.
    void set_roi_overlay(int x, int y, int w, int h, bool enabled);

signals:
    /// @brief Emitted when the user finishes a drag in ROI drag mode.
    /// Coordinates are in sensor pixels.
    void roi_dragged(int x, int y, int w, int h);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void draw_letterboxed(int widget_w, int widget_h, int img_w, int img_h);
    /// @brief Computes the letterboxed viewport rect in logical (widget)
    /// pixels for the currently displayed frame.
    bool compute_logical_letterbox(QRect& out) const;
    /// @brief Maps a widget-pixel point to sensor-pixel coordinates.
    bool widget_to_sensor(const QPoint& widget_pos, QPoint& sensor_pos) const;
    void draw_overlay(QPainter& painter);

    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLVertexArrayObject> vao_;
    std::unique_ptr<QOpenGLTexture> texture_;
    int tex_loc_{0};

    QImage frame_;
    bool frame_dirty_{false};
    QLabel* overlay_label_{nullptr};

    // ROI drag mode + persistent overlay.
    bool roi_drag_mode_{false};
    bool dragging_{false};
    QPoint drag_start_widget_;
    QPoint drag_curr_widget_;
    QRect roi_overlay_sensor_;
    bool roi_overlay_enabled_{false};
};

} // namespace gui

#endif // GUI_DISPLAY_EVENT_DISPLAY_WIDGET_H
