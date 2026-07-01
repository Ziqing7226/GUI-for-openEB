// gui/display/space_time_display.h — XYT 3D event point-cloud display.
//
// Design §4.3.25 and §1.6.6 (jAER SpaceTimeRollingEventDisplayMethod). A
// QOpenGLWidget that renders the recent event stream as a 3D point cloud:
// X = pixel column, Y = pixel row, T = time (depth axis). Polarity colouring
// (ON = red / OFF = green) or an age gradient (blue → green → red) is
// provided by algo/cv/xyt_visualizer.h; this widget owns the VBO + GLSL
// rendering and the orbit camera.
//
// Replaces the legacy 2D TemporalPlotWindow (kept for backward compat).

#ifndef GUI_DISPLAY_SPACE_TIME_DISPLAY_H
#define GUI_DISPLAY_SPACE_TIME_DISPLAY_H

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <memory>
#include <vector>

#include <metavision/sdk/base/events/event_cd.h>

#include "algo/cv/xyt_visualizer.h"
#include "algo/common/event.h"

class QWheelEvent;
class QMouseEvent;

namespace gui {

class SpaceTimeDisplay : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit SpaceTimeDisplay(QWidget* parent = nullptr);
    ~SpaceTimeDisplay();

    /// @brief Sets the sensor geometry (used to centre the point cloud).
    void set_sensor_geometry(int width, int height);

    /// @brief Feeds a batch of events. Thread-safe via Qt::QueuedConnection
    /// if called from a non-GUI thread.
    void push_events(const Metavision::EventCD* begin, const Metavision::EventCD* end);

public slots:
    void set_time_window_ms(float ms);
    void set_point_size(float s);
    void set_color_mode(int mode);          ///< 0 = Polarity, 1 = Age
    void set_auto_rotate(bool on);
    void set_depth_shade(bool on);
    void clear();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuild_vbo();
    void update_camera_uniforms();

    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLVertexArrayObject> vao_;
    std::unique_ptr<QOpenGLBuffer> vbo_;

    gui_algo::XYTVisualizer viz_;
    std::vector<gui_algo::XYTPoint> points_;
    int point_count_{0};
    int vbo_capacity_bytes_{0};
    bool gl_ready_{false};

    int sensor_w_{0};
    int sensor_h_{0};

    // Orbit camera.
    float azimuth_{-58.0f};    ///< degrees
    float elevation_{8.0f};    ///< degrees
    float distance_{4.0f};     ///< relative to scene scale
    bool dragging_{false};
    QPoint last_mouse_;
    bool auto_rotate_{false};
};

} // namespace gui

#endif // GUI_DISPLAY_SPACE_TIME_DISPLAY_H
