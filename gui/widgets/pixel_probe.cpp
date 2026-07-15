// gui/widgets/pixel_probe.cpp

#include "pixel_probe.h"

#include <QCloseEvent>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gui {

PixelProbe::PixelProbe(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(tr("Pixel Probe"));
    setMinimumSize(360, 360);

    isi_hist_.assign(kIsiBins, 0);
    isi_bin_edges_us_.assign(kIsiBins + 1, 0.0);
    const double max_us = kIsiMaxMs * 1000.0;
    for (int i = 0; i <= kIsiBins; ++i) {
        isi_bin_edges_us_[i] = max_us * i / kIsiBins;
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    header_label_ = new QLabel(tr("Click a pixel in the main display to inspect."), this);
    header_label_->setWordWrap(true);
    layout->addWidget(header_label_);
    // The paintEvent renders the detailed stats below the header label.
}

PixelProbe::~PixelProbe() = default;

void PixelProbe::set_sensor_geometry(int width, int height) {
    sensor_w_ = width;
    sensor_h_ = height;
}

void PixelProbe::push_events(const Metavision::EventCD* begin, const Metavision::EventCD* end) {
    if (begin == nullptr || end == nullptr || begin >= end) return;
    for (const Metavision::EventCD* e = begin; e != end; ++e) {
        ProbeEvent pe;
        pe.x = e->x;
        pe.y = e->y;
        pe.p = static_cast<std::int8_t>(e->p ? 1 : 0);
        pe.t = e->t;
        buffer_.push_back(pe);
    }
    prune_buffer();
    if (sel_x_ >= 0) {
        recompute();
        update();
    }
}

void PixelProbe::prune_buffer() {
    while (buffer_.size() > kMaxBuffer) {
        buffer_.pop_front();
    }
}

void PixelProbe::set_selected_pixel(int x, int y) {
    if (sensor_w_ > 0 && x >= sensor_w_) x = sensor_w_ - 1;
    if (sensor_h_ > 0 && y >= sensor_h_) y = sensor_h_ - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    sel_x_ = x;
    sel_y_ = y;
    header_label_->setText(tr("Inspecting pixel (%1, %2)").arg(x).arg(y));
    recompute();
    update();
}

void PixelProbe::clear() {
    buffer_.clear();
    pixel_events_.clear();
    total_count_ = 0;
    on_count_ = 0;
    off_count_ = 0;
    mean_isi_us_ = 0.0;
    rate_keps_ = 0.0;
    std::fill(isi_hist_.begin(), isi_hist_.end(), 0);
    update();
}

void PixelProbe::recompute() {
    pixel_events_.clear();
    total_count_ = 0;
    on_count_ = 0;
    off_count_ = 0;
    mean_isi_us_ = 0.0;
    rate_keps_ = 0.0;
    std::fill(isi_hist_.begin(), isi_hist_.end(), 0);

    if (sel_x_ < 0) return;
    const std::uint16_t tx = static_cast<std::uint16_t>(sel_x_);
    const std::uint16_t ty = static_cast<std::uint16_t>(sel_y_);

    for (const auto& e : buffer_) {
        if (e.x == tx && e.y == ty) {
            pixel_events_.push_back(e);
        }
    }

    total_count_ = static_cast<long>(pixel_events_.size());
    if (total_count_ == 0) return;

    for (const auto& e : pixel_events_) {
        if (e.p) ++on_count_;
        else ++off_count_;
    }

    // ISI histogram + mean ISI.
    const double max_us = kIsiMaxMs * 1000.0;
    double isi_sum = 0.0;
    long isi_count = 0;
    for (std::size_t i = 1; i < pixel_events_.size(); ++i) {
        const double isi = static_cast<double>(pixel_events_[i].t - pixel_events_[i - 1].t);
        if (isi < 0.0) continue;  // ignore out-of-order (shouldn't happen)
        isi_sum += isi;
        ++isi_count;
        int bin = static_cast<int>(isi / max_us * kIsiBins);
        if (bin < 0) bin = 0;
        if (bin >= kIsiBins) bin = kIsiBins - 1;
        ++isi_hist_[bin];
    }
    if (isi_count > 0) {
        mean_isi_us_ = isi_sum / isi_count;
        if (mean_isi_us_ > 0.0) {
            rate_keps_ = 1000.0 / mean_isi_us_;  // 1/us = 1000/s → keps
        }
    }
}

void PixelProbe::closeEvent(QCloseEvent* event) {
    emit closed();
    QWidget::closeEvent(event);
}

void PixelProbe::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont mono("Monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);
    p.setFont(mono);
    const QFontMetrics fm(mono);

    const int margin = 8;
    int y = header_label_->geometry().bottom() + margin;
    const int width = this->width();

    auto line = [&](const QString& s) {
        p.drawText(margin, y, s);
        y += fm.lineSpacing();
    };

    if (sel_x_ < 0) {
        p.setPen(Qt::gray);
        line(tr("No pixel selected."));
        return;
    }

    p.setPen(Qt::white);
    line(tr("Pixel: (%1, %2)").arg(sel_x_).arg(sel_y_));
    line(tr("Events in buffer: %1").arg(buffer_.size()));
    line(tr("Events at pixel:  %1").arg(total_count_));
    line(tr("  ON:  %1   OFF: %2")
             .arg(on_count_)
             .arg(off_count_));
    line(tr("Mean ISI: %1 us   (%2 keps)")
             .arg(mean_isi_us_, 0, 'f', 1)
             .arg(rate_keps_, 0, 'f', 2));
    y += fm.lineSpacing() / 2;

    // ISI histogram as simple bars.
    p.setPen(Qt::white);
    line(tr("ISI histogram (0..%1 ms):").arg(kIsiMaxMs, 0, 'f', 0));
    long max_bin = *std::max_element(isi_hist_.begin(), isi_hist_.end());
    if (max_bin <= 0) max_bin = 1;
    const int bar_area_w = width - 2 * margin - 120;
    for (int i = 0; i < kIsiBins; ++i) {
        const long c = isi_hist_[i];
        const double lo_ms = isi_bin_edges_us_[i] / 1000.0;
        const double hi_ms = isi_bin_edges_us_[i + 1] / 1000.0;
        const QString label = QString("%1-%2ms")
                                  .arg(lo_ms, 0, 'f', 0)
                                  .arg(hi_ms, 0, 'f', 0);
        p.setPen(Qt::lightGray);
        p.drawText(margin, y, label);
        const int bar_w = static_cast<int>(bar_area_w * static_cast<double>(c) / max_bin);
        p.setPen(Qt::NoPen);
        p.setBrush(c > 0 ? QColor(80, 180, 255) : QColor(60, 60, 60));
        p.drawRect(margin + 90, y - fm.ascent() + 1, std::max(bar_w, 1), fm.ascent());
        p.setPen(Qt::white);
        p.drawText(margin + 90 + bar_area_w + 4, y, QString::number(c));
        y += fm.lineSpacing();
    }
    y += fm.lineSpacing() / 2;

    // Recent timestamps (last N events).
    const std::size_t kRecent = 8;
    p.setPen(Qt::white);
    line(tr("Recent events:"));
    const std::size_t start = pixel_events_.size() > kRecent
                                  ? pixel_events_.size() - kRecent
                                  : 0;
    for (std::size_t i = start; i < pixel_events_.size(); ++i) {
        const auto& e = pixel_events_[i];
        const QString pol = e.p ? "ON " : "OFF";
        line(QString("  t=%1 us  %2").arg(e.t).arg(pol));
    }
}

} // namespace gui
