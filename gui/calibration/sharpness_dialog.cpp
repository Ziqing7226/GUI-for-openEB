// gui/calibration/sharpness_dialog.cpp

#include "sharpness_dialog.h"

#include <QDateTime>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QTimer>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

#include "display/event_display_widget.h"

namespace gui {

namespace {
// Poll interval — 10 Hz. Fast enough to feel live during bias / focus tuning,
// cheap enough to not compete with the rendering loop.
constexpr int kPollIntervalMs = 100;

/// @brief Theoretical upper bound for the variance-of-Laplacian sharpness
///        metric on a W×H 8-bit grayscale frame, assuming an ideal
///        single-pixel checkerboard (the sharpest possible pattern).
///
/// cv::Laplacian ksize=3, scale=1, delta=0, BORDER_REPLICATE. The kernel is
///   [0  1  0]
///   [1 -4  1]
///   [0  1  0]
/// so L(x,y) = I(x-1,y) + I(x+1,y) + I(x,y-1) + I(x,y+1) - 4·I(x,y).
///
/// For a checkerboard, half the pixels have L = +|L| and half have L = -|L|,
/// so the mean is 0 and σ² = E[L²]. The |L| depends on how many of the 4
/// neighbors are the opposite color (BORDER_REPLICATE copies the center into
/// out-of-bounds positions):
///   interior (4 opposite neighbors):           |L| = 4·255 = 1020
///   edge      (3 opposite, 1 replicated same): |L| = 3·255 = 765
///   corner    (2 opposite, 2 replicated same): |L| = 2·255 = 510
///
/// σ²_max = [n_int·1020² + n_edge·765² + n_corner·510²] / (W·H)
///
/// The result is slightly resolution-dependent (boundary effects) and
/// converges to 1020² = 1 040 400 for large sensors. Real event frames never
/// reach this bound, but it gives the chart a stable, principled Y-axis
/// ceiling instead of auto-scaling jitter.
double compute_theoretical_max_variance(int W, int H) {
    if (W < 2 || H < 2) return 1040400.0;  // fallback: interior-only bound
    const double li = 4.0 * 255.0;   // 1020 — interior pixel |L|
    const double le = 3.0 * 255.0;   // 765  — edge pixel |L|
    const double lc = 2.0 * 255.0;   // 510  — corner pixel |L|
    const long n_int    = static_cast<long>(W - 2) * (H - 2);
    const long n_edge   = 2L * (W - 2) + 2L * (H - 2);
    const long n_corner = 4L;
    const long N = static_cast<long>(W) * H;
    const double sum_sq = static_cast<double>(n_int) * li * li
                        + static_cast<double>(n_edge) * le * le
                        + static_cast<double>(n_corner) * lc * lc;
    return sum_sq / static_cast<double>(N);
}
} // namespace

// ---------------------------------------------------------------------------
// SharpnessChart
// ---------------------------------------------------------------------------

SharpnessChart::SharpnessChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 140);
    setAutoFillBackground(true);
    // Dark background like an oscilloscope screen.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(24, 24, 28));
    setPalette(pal);
}

void SharpnessChart::add_value(double value) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    samples_.push_back({now, value});
    // Prune samples older than the window.
    const qint64 cutoff = now - kWindowMs;
    while (!samples_.empty() && samples_.front().t_ms < cutoff) {
        samples_.erase(samples_.begin());
    }
    update();
}

void SharpnessChart::clear() {
    samples_.clear();
    update();
}

void SharpnessChart::set_y_max(double max) {
    y_max_ = max;
    // No update() here — the caller (on_tick) follows with add_value(), which
    // triggers a repaint. Calling update() here would double-repaint.
}

void SharpnessChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(0, 0, -1, -1);

    // Background.
    p.fillRect(r, QColor(24, 24, 28));

    // Chart area with margins for labels.
    const int ml = 8, mt = 10, mr = 8, mb = 8;
    const QRectF chart_r(r.left() + ml, r.top() + mt,
                         r.width() - ml - mr, r.height() - mt - mb);

    // Grid — 4 horizontal lines.
    p.setPen(QPen(QColor(50, 50, 55), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const qreal y = chart_r.top() + chart_r.height() * i / 4.0;
        p.drawLine(QPointF(chart_r.left(), y), QPointF(chart_r.right(), y));
    }

    if (samples_.empty()) {
        p.setPen(QColor(130, 130, 135));
        QFont f = p.font();
        f.setPointSize(11);
        p.setFont(f);
        p.drawText(chart_r, Qt::AlignCenter, tr("Waiting for data..."));
        return;
    }

    // Y-axis range: use the fixed theoretical upper bound if set (stable
    // scale — no auto-scaling jitter), else fall back to auto-scaling from
    // the visible samples.
    double y_min, y_max, label_max;
    if (y_max_ > 0.0) {
        // Fixed ceiling: [0, theoretical_max]. Sharpness is always ≥ 0.
        y_min = 0.0;
        y_max = y_max_;
        label_max = y_max_;
    } else {
        // Auto-scale from data.
        y_min = samples_.front().value;
        y_max = samples_.front().value;
        for (const auto& s : samples_) {
            if (s.value < y_min) y_min = s.value;
            if (s.value > y_max) y_max = s.value;
        }
        if (y_max - y_min < 1.0) {
            // Avoid degenerate range — pad around the midpoint.
            const double mid = 0.5 * (y_max + y_min);
            y_min = mid - 0.5;
            y_max = mid + 0.5;
        }
        // Add 10 % headroom top and bottom.
        const double y_range = y_max - y_min;
        y_min -= y_range * 0.1;
        y_max += y_range * 0.1;
        label_max = y_max - y_range * 0.1;  // data max without headroom
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 t_min = now - 2000;  // 2-second window

    // Map (t, value) → (x, y) in chart coordinates.
    auto map_pt = [&](const Sample& s) -> QPointF {
        const double x_frac = static_cast<double>(s.t_ms - t_min) / 2000.0;
        const double y_frac = (s.value - y_min) / (y_max - y_min);
        const qreal x = chart_r.left() + x_frac * chart_r.width();
        const qreal y = chart_r.bottom() - y_frac * chart_r.height();
        return QPointF(x, y);
    };

    // Draw the polyline.
    p.setPen(QPen(QColor(76, 200, 130), 2));
    QPainterPath path;
    bool first = true;
    for (const auto& s : samples_) {
        const QPointF pt = map_pt(s);
        if (first) { path.moveTo(pt); first = false; }
        else       { path.lineTo(pt); }
    }
    p.drawPath(path);

    // Draw sample dots.
    p.setBrush(QColor(76, 200, 130));
    p.setPen(Qt::NoPen);
    for (const auto& s : samples_) {
        p.drawEllipse(map_pt(s), 2.5, 2.5);
    }

    // Current value (top-left) and Y range (top-right).
    const double current = samples_.back().value;
    p.setPen(QColor(235, 235, 240));
    QFont f = p.font();
    f.setPointSize(11);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(chart_r.left(), r.top(), chart_r.width() * 0.6, mt + 2),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("σ² = %1").arg(current, 0, 'f', 1));

    f.setBold(false);
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(QColor(140, 140, 145));
    p.drawText(QRectF(chart_r.right() - chart_r.width() * 0.4, r.top(),
                      chart_r.width() * 0.4, mt + 2),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("max %1").arg(label_max, 0, 'f', 0));
}

// ---------------------------------------------------------------------------
// SharpnessDialog
// ---------------------------------------------------------------------------

SharpnessDialog::SharpnessDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Sharpness Meter"));
    setMinimumSize(320, 200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    chart_ = new SharpnessChart(this);
    layout->addWidget(chart_, 1);

    hint_label_ = new QLabel(
        tr("Variance of Laplacian of the current event frame.\n"
           "Higher = sharper. Point at a high-contrast scene."),
        this);
    hint_label_->setWordWrap(true);
    hint_label_->setAlignment(Qt::AlignCenter);
    hint_label_->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint_label_);

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::CoarseTimer);
    timer_->setInterval(kPollIntervalMs);
    connect(timer_, &QTimer::timeout, this, &SharpnessDialog::on_tick);
    timer_->start();
}

SharpnessDialog::~SharpnessDialog() {
    if (timer_) timer_->stop();
}

void SharpnessDialog::set_display(EventDisplayWidget* display) {
    display_ = display;
}

void SharpnessDialog::on_tick() {
    if (!display_) {
        chart_->clear();
        return;
    }
    const QImage img = display_->current_frame();
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) {
        chart_->clear();
        return;
    }

    // Convert the QImage to a single-channel cv::Mat. The event display widget
    // hands out whatever format the frame pipeline produced (typically
    // Format_RGB888 for annotated frames, Format_Grayscale8 / Format_Indexed8
    // for raw event frames). Handle the common cases.
    cv::Mat gray;
    switch (img.format()) {
        case QImage::Format_Grayscale8:
            gray = cv::Mat(img.height(), img.width(), CV_8UC1,
                           const_cast<uchar*>(img.bits()),
                           static_cast<std::size_t>(img.bytesPerLine())).clone();
            break;
        case QImage::Format_RGB888: {
            cv::Mat rgb(img.height(), img.width(), CV_8UC3,
                        const_cast<uchar*>(img.bits()),
                        static_cast<std::size_t>(img.bytesPerLine()));
            cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
            break;
        }
        case QImage::Format_BGR888: {
            cv::Mat bgr(img.height(), img.width(), CV_8UC3,
                        const_cast<uchar*>(img.bits()),
                        static_cast<std::size_t>(img.bytesPerLine()));
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
            break;
        }
        case QImage::Format_ARGB32:
        case QImage::Format_RGBA8888: {
            cv::Mat rgba(img.height(), img.width(), CV_8UC4,
                         const_cast<uchar*>(img.bits()),
                         static_cast<std::size_t>(img.bytesPerLine()));
            cv::cvtColor(rgba, gray, cv::COLOR_RGBA2GRAY);
            break;
        }
        default: {
            // Fallback: convert to Format_RGB888 first, then to gray.
            const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
            cv::Mat m(rgb.height(), rgb.width(), CV_8UC3,
                      const_cast<uchar*>(rgb.bits()),
                      static_cast<std::size_t>(rgb.bytesPerLine()));
            cv::cvtColor(m, gray, cv::COLOR_RGB2GRAY);
            break;
        }
    }
    if (gray.empty()) {
        chart_->clear();
        return;
    }

    // Variance of Laplacian: the standard focus measure. cv::Laplacian with
    // ksize=3 on a CV_64F buffer; σ² is the variance.
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F, 3, 1, 0, cv::BORDER_REPLICATE);
    cv::Scalar mean, sigma;
    cv::meanStdDev(lap, mean, sigma);
    const double variance = sigma[0] * sigma[0];

    // Fix the chart's Y-axis ceiling to the theoretical maximum for this
    // frame's resolution (ideal single-pixel checkerboard). Keeps the scale
    // stable across time — the line doesn't jump as the data range shifts.
    chart_->set_y_max(compute_theoretical_max_variance(gray.cols, gray.rows));

    chart_->add_value(variance);
}

} // namespace gui
