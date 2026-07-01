// gui/display/space_time_display.cpp

#include "space_time_display.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QVector3D>
#include <QWheelEvent>

#include <cmath>
#include <cstdint>

namespace gui {

namespace {
constexpr const char* kVertSrc = R"GLSL(#version 330 core
in vec3 aPos;     // (t_norm, x_norm, 1 - y_norm), each in [0,1]
in vec3 aColor;
out vec3 vColor;
out float vAge;
uniform mat4 uMVP;
uniform float uPointSize;
uniform bool uDepthShade;
void main() {
    // Centre the unit cube around the origin.
    vec3 p = aPos - vec3(0.5, 0.5, 0.5);
    gl_Position = uMVP * vec4(p, 1.0);
    vAge = aPos.x;
    float ps = uPointSize;
    if (uDepthShade) {
        ps *= 0.5 + 0.5 * vAge;
    }
    gl_PointSize = ps;
    vColor = aColor;
}
)GLSL";

constexpr const char* kFragSrc = R"GLSL(#version 330 core
in vec3 vColor;
in float vAge;
out vec4 fragColor;
uniform bool uDepthShade;
void main() {
    // Round points (discard corners) + optional depth dimming.
    vec2 d = gl_PointCoord - vec2(0.5);
    float r2 = dot(d, d);
    if (r2 > 0.25) discard;
    vec3 c = vColor;
    if (uDepthShade) {
        c *= (0.3 + 0.7 * vAge);
    }
    fragColor = vec4(c, 0.75);
}
)GLSL";
} // namespace

SpaceTimeDisplay::SpaceTimeDisplay(QWidget* parent) : QOpenGLWidget(parent) {
    setWindowTitle(tr("Space-Time 3D"));
    setMinimumSize(320, 240);
    setSizePolicy(sizePolicy().horizontalPolicy(), sizePolicy().verticalPolicy());

    QSurfaceFormat fmt = format();
    fmt.setSamples(4);  // MSAA for nicer points
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
}

SpaceTimeDisplay::~SpaceTimeDisplay() {
    // GL resources must be freed with a current context.
    if (gl_ready_) {
        makeCurrent();
        vbo_.reset();
        vao_.reset();
        program_.reset();
        doneCurrent();
    }
}

void SpaceTimeDisplay::set_sensor_geometry(int width, int height) {
    sensor_w_ = (width > 0) ? width : 0;
    sensor_h_ = (height > 0) ? height : 0;
}

void SpaceTimeDisplay::push_events(const Metavision::EventCD* begin,
                                   const Metavision::EventCD* end) {
    if (begin == nullptr || end == nullptr || begin >= end) return;
    const std::size_t n = static_cast<std::size_t>(end - begin);
    // gui_algo::Event is layout-compatible with EventCD; reinterpret-cast view.
    const gui_algo::Event* ev = reinterpret_cast<const gui_algo::Event*>(begin);
    viz_.process(ev, n);
    update();
}

void SpaceTimeDisplay::set_time_window_ms(float ms) {
    viz_.set_time_window_ms(ms);
    update();
}

void SpaceTimeDisplay::set_point_size(float s) {
    viz_.set_point_size(s);
    update();
}

void SpaceTimeDisplay::set_color_mode(int mode) {
    viz_.set_color_mode(mode == 1 ? gui_algo::XYTVisualizer::ColorMode::Age
                                  : gui_algo::XYTVisualizer::ColorMode::Polarity);
    update();
}

void SpaceTimeDisplay::set_auto_rotate(bool on) {
    auto_rotate_ = on;
    update();
}

void SpaceTimeDisplay::set_depth_shade(bool on) {
    viz_.set_depth_shade(on);
    update();
}

void SpaceTimeDisplay::clear() {
    viz_.clear();
    points_.clear();
    point_count_ = 0;
    update();
}

void SpaceTimeDisplay::initializeGL() {
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    program_ = std::make_unique<QOpenGLShaderProgram>();
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
    program_->bindAttributeLocation("aPos", 0);
    program_->bindAttributeLocation("aColor", 1);
    program_->link();

    vao_ = std::make_unique<QOpenGLVertexArrayObject>();
    vao_->create();
    vao_->bind();

    vbo_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    vbo_->create();
    vbo_->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    vbo_->bind();

    const int kInitialVboBytes = 65536 * 6 * static_cast<int>(sizeof(float));
    vbo_->allocate(nullptr, kInitialVboBytes);
    vbo_capacity_bytes_ = kInitialVboBytes;

    // stride = 6 floats (3 pos + 3 color)
    const int stride = 6 * static_cast<int>(sizeof(float));
    program_->enableAttributeArray(0);
    program_->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
    program_->enableAttributeArray(1);
    program_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, stride);

    vao_->release();
    vbo_->release();

    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    gl_ready_ = true;
}

void SpaceTimeDisplay::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void SpaceTimeDisplay::rebuild_vbo() {
    points_ = viz_.render();
    point_count_ = static_cast<int>(points_.size());
    if (!gl_ready_ || !vbo_) return;

    // Build a flat float buffer of normalized positions + colors.
    const float sx = (sensor_w_ > 0) ? 1.0f / static_cast<float>(sensor_w_) : 1.0f;
    const float sy = (sensor_h_ > 0) ? 1.0f / static_cast<float>(sensor_h_) : 1.0f;

    std::vector<float> data;
    data.reserve(static_cast<std::size_t>(point_count_) * 6);
    for (const auto& p : points_) {
        data.push_back(p.t);
        data.push_back(p.x * sx);
        data.push_back(1.0f - p.y * sy);
        data.push_back(p.r);
        data.push_back(p.g);
        data.push_back(p.b);
    }

    vbo_->bind();
    const int needed = static_cast<int>(data.size() * sizeof(float));
    if (needed > vbo_capacity_bytes_) {
        const int new_cap = std::max(needed, vbo_capacity_bytes_ * 2);
        vbo_->allocate(nullptr, new_cap);
        vbo_capacity_bytes_ = new_cap;
    }
    vbo_->write(0, data.data(), needed);
    vbo_->release();
}

void SpaceTimeDisplay::update_camera_uniforms() {
    const float az = azimuth_ * static_cast<float>(M_PI) / 180.0f;
    const float el = elevation_ * static_cast<float>(M_PI) / 180.0f;
    const float dist = distance_;

    QVector3D eye(dist * std::cos(el) * std::sin(az),
                  dist * std::sin(el),
                  dist * std::cos(el) * std::cos(az));

    QMatrix4x4 view;
    view.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 1, 0));

    QMatrix4x4 proj;
    proj.perspective(45.0f,
                     static_cast<float>(width()) / static_cast<float>(std::max(height(), 1)),
                     0.05f, 100.0f);

    QMatrix4x4 model;
    model.scale(3.0f, 1.0f, 1.0f);

    QMatrix4x4 mvp = proj * view * model;
    program_->setUniformValue("uMVP", mvp);
    program_->setUniformValue("uPointSize", viz_.point_size());
    program_->setUniformValue("uDepthShade", viz_.depth_shade());
}

void SpaceTimeDisplay::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!gl_ready_ || !program_) return;

    if (auto_rotate_) {
        azimuth_ += 0.4f;
        if (azimuth_ > 360.0f) azimuth_ -= 360.0f;
    }

    rebuild_vbo();
    if (point_count_ == 0) {
        return;  // nothing to draw
    }

    program_->bind();
    vao_->bind();
    update_camera_uniforms();
    glDrawArrays(GL_POINTS, 0, point_count_);
    vao_->release();
    program_->release();

    if (auto_rotate_) {
        update();  // keep animating
    }
}

void SpaceTimeDisplay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        last_mouse_ = event->position().toPoint();
    }
}

void SpaceTimeDisplay::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint cur = event->position().toPoint();
    const QPoint delta = cur - last_mouse_;
    azimuth_ += delta.x() * 0.4f;
    elevation_ -= delta.y() * 0.4f;
    if (elevation_ > 89.0f) elevation_ = 89.0f;
    if (elevation_ < -89.0f) elevation_ = -89.0f;
    last_mouse_ = cur;
    update();
}

void SpaceTimeDisplay::mouseReleaseEvent(QMouseEvent* /*event*/) {
    dragging_ = false;
}

void SpaceTimeDisplay::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta > 0) {
        distance_ *= 0.9f;
    } else if (delta < 0) {
        distance_ *= 1.1f;
    }
    if (distance_ < 0.3f) distance_ = 0.3f;
    if (distance_ > 20.0f) distance_ = 20.0f;
    update();
}

} // namespace gui
