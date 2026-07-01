// gui/display/frame_annotator.cpp

#include "frame_annotator.h"

#include <QBrush>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <cmath>

namespace gui {

FrameAnnotator::FrameAnnotator() = default;

void FrameAnnotator::set_pen_width(double w) {
    pen_width_ = (w > 0.0) ? w : 1.0;
}

void FrameAnnotator::set_font(const QFont& font) {
    font_ = font;
}

void FrameAnnotator::set_default_color(const QColor& c) {
    if (c.isValid()) default_color_ = c;
}

QColor FrameAnnotator::resolve(const QColor& c) const {
    return c.isValid() ? c : default_color_;
}

void FrameAnnotator::draw_bboxes(QImage& img, const std::vector<QRect>& boxes,
                                 const QColor& color) {
    if (img.isNull() || boxes.empty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(resolve(color), pen_width_));
    p.setBrush(Qt::NoBrush);
    for (const QRect& r : boxes) {
        p.drawRect(r);
    }
}

void FrameAnnotator::draw_boxes(QImage& img, const std::vector<Box>& boxes,
                                const QColor& color) {
    if (img.isNull() || boxes.empty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(font_);
    const QFontMetrics fm(font_);
    for (const Box& b : boxes) {
        const QColor c = resolve(color);
        p.setPen(QPen(c, pen_width_));
        p.setBrush(Qt::NoBrush);
        p.drawRect(b.rect);

        // Caption (id + label) with a solid background bar for legibility.
        QString caption;
        if (b.id >= 0) caption = QString("#%1").arg(b.id);
        if (!b.label.isEmpty()) {
            caption = caption.isEmpty() ? b.label : caption + " " + b.label;
        }
        if (!caption.isEmpty()) {
            const int tw = fm.horizontalAdvance(caption) + 6;
            const int th = fm.height() + 2;
            int by = b.rect.top() - th;
            if (by < 0) by = b.rect.top();
            p.fillRect(b.rect.left(), by, tw, th, c);
            p.setPen(QPen(Qt::black));
            p.drawText(b.rect.left() + 3, by + fm.ascent() + 1, caption);
            p.setPen(QPen(c, pen_width_));
        }
    }
}

void FrameAnnotator::draw_text(QImage& img, const QString& text, const QPoint& pos,
                               const QColor& color, bool with_background) {
    if (img.isNull() || text.isEmpty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(font_);
    const QFontMetrics fm(font_);
    const QRect text_rect = fm.boundingRect(text);
    QRect box = text_rect.adjusted(-3, -1, 3, 1);
    box.moveTopLeft(pos);
    if (with_background) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 3, 3);
    }
    p.setPen(QPen(resolve(color)));
    p.drawText(box, Qt::AlignCenter, text);
}

void FrameAnnotator::draw_lines(QImage& img, const std::vector<QLineF>& lines,
                                const QColor& color) {
    if (img.isNull() || lines.empty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(resolve(color), pen_width_));
    p.setBrush(Qt::NoBrush);
    for (const QLineF& l : lines) {
        p.drawLine(l);
    }
}

void FrameAnnotator::draw_points(QImage& img, const std::vector<QPointF>& points,
                                 double radius, const QColor& color) {
    if (img.isNull() || points.empty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = resolve(color);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const double r = (radius > 0.0) ? radius : 1.0;
    for (const QPointF& pt : points) {
        p.drawEllipse(pt, r, r);
    }
}

void FrameAnnotator::draw_circles(QImage& img,
                                  const std::vector<std::pair<QPointF, double>>& circles,
                                  const QColor& color) {
    if (img.isNull() || circles.empty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(resolve(color), pen_width_));
    p.setBrush(Qt::NoBrush);
    for (const auto& cir : circles) {
        p.drawEllipse(cir.first, cir.second, cir.second);
    }
}

void FrameAnnotator::draw_flow_arrows(QImage& img, const std::vector<FlowArrow>& arrows,
                                      const QColor& color) {
    if (img.isNull() || arrows.empty()) return;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = resolve(color);
    p.setPen(QPen(c, pen_width_));
    p.setBrush(c);
    for (const FlowArrow& a : arrows) {
        p.drawLine(a.from, a.to);
        // Arrowhead: two short segments at the tip.
        const double dx = a.to.x() - a.from.x();
        const double dy = a.to.y() - a.from.y();
        const double len = std::hypot(dx, dy);
        if (len < 1e-3) continue;
        const double ux = dx / len;
        const double uy = dy / len;
        const double head = std::min(8.0, len * 0.4);
        const double ang = 0.4;  // ~23°
        const QPointF h1(a.to.x() - head * (ux * std::cos(ang) + uy * std::sin(ang)),
                         a.to.y() - head * (uy * std::cos(ang) - ux * std::sin(ang)));
        const QPointF h2(a.to.x() - head * (ux * std::cos(ang) - uy * std::sin(ang)),
                         a.to.y() - head * (uy * std::cos(ang) + ux * std::sin(ang)));
        p.drawLine(a.to, h1);
        p.drawLine(a.to, h2);
    }
}

} // namespace gui
