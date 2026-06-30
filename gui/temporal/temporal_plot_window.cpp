// gui/temporal/temporal_plot_window.cpp

#include "temporal_plot_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>

namespace gui {

TemporalPlotWindow::TemporalPlotWindow(QWidget* parent)
    : QWidget(parent, Qt::Window) {  // Qt::Window → top-level, has title bar + close button
    setWindowTitle(tr("Temporal Plot"));
    setMinimumSize(640, 360);

    auto* outer = new QVBoxLayout(this);

    // Controls row — all five design §3.2.3 parameters.
    auto* ctrl_row = new QHBoxLayout();

    ctrl_row->addWidget(new QLabel(tr("Axis:"), this));
    mode_combo_ = new QComboBox(this);
    mode_combo_->addItem(tr("X-T"), static_cast<int>(Mode::XT));
    mode_combo_->addItem(tr("Y-T"), static_cast<int>(Mode::YT));
    ctrl_row->addWidget(mode_combo_);

    ctrl_row->addWidget(new QLabel(tr("Position:"), this));
    position_spin_ = new QSpinBox(this);
    position_spin_->setRange(0, 9999);
    position_spin_->setValue(0);
    ctrl_row->addWidget(position_spin_);

    ctrl_row->addWidget(new QLabel(tr("Accum:"), this));
    accumulation_spin_ = new QSpinBox(this);
    accumulation_spin_->setRange(1, 1000);
    accumulation_spin_->setSingleStep(10);
    accumulation_spin_->setValue(accumulation_ms_);
    accumulation_spin_->setSuffix(" ms");
    ctrl_row->addWidget(accumulation_spin_);

    ctrl_row->addWidget(new QLabel(tr("Window:"), this));
    window_spin_ = new QSpinBox(this);
    window_spin_->setRange(50, 10000);
    window_spin_->setSingleStep(50);
    window_spin_->setValue(window_ms_);
    window_spin_->setSuffix(" ms");
    ctrl_row->addWidget(window_spin_);

    polarity_check_ = new QCheckBox(tr("Polarity"), this);
    polarity_check_->setChecked(show_polarity_);
    ctrl_row->addWidget(polarity_check_);

    ctrl_row->addStretch();

    status_label_ = new QLabel(tr("0 samples"), this);
    ctrl_row->addWidget(status_label_);

    clear_btn_ = new QPushButton(tr("Clear"), this);
    ctrl_row->addWidget(clear_btn_);

    outer->addLayout(ctrl_row);

    setAutoFillBackground(true);

    // Wire signals.
    connect(mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TemporalPlotWindow::on_mode_changed);
    connect(position_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TemporalPlotWindow::on_position_changed);
    connect(accumulation_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { accumulation_ms_ = std::max(1, v); });
    connect(window_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TemporalPlotWindow::set_time_window_ms);
    connect(polarity_check_, &QCheckBox::toggled, this,
            [this](bool on) { show_polarity_ = on; update(); });
    connect(clear_btn_, &QPushButton::clicked, this, &TemporalPlotWindow::clear);
}

TemporalPlotWindow::~TemporalPlotWindow() = default;

void TemporalPlotWindow::set_sensor_geometry(int width, int height) {
    sensor_w_ = std::max(0, width);
    sensor_h_ = std::max(0, height);
    update_position_range();
    update();
}

void TemporalPlotWindow::push_events(const Metavision::EventCD* begin,
                                     const Metavision::EventCD* end) {
    if (begin == end) return;

    // Cross-section filter (design §3.2.3 axis_position):
    //   X-T mode → keep events on row y == axis_position_, plot x vs t
    //   Y-T mode → keep events on col x == axis_position_, plot y vs t
    const int target = axis_position_;
    for (const Metavision::EventCD* it = begin; it != end; ++it) {
        if (mode_ == Mode::XT) {
            if (it->y != target) continue;
        } else {
            if (it->x != target) continue;
        }
        Sample s;
        s.t = it->t;
        s.coord = (mode_ == Mode::XT) ? it->x : it->y;
        s.pol = it->p;
        samples_.push_back(s);
    }

    if (samples_.empty()) return;

    // Cap memory: keep at most 500k samples.
    const std::size_t max_keep = 500000;
    if (samples_.size() > max_keep) {
        samples_.erase(samples_.begin(),
                       samples_.begin() + (samples_.size() - max_keep));
    }

    t_max_ = samples_.back().t;
    prune_old_samples();

    // Accumulation throttle (design §3.2.3 accumulation_time_ms): only
    // repaint if enough time has elapsed since the last refresh. Events
    // arriving between refreshes are accumulated silently into samples_.
    const Metavision::timestamp accum_us =
        static_cast<Metavision::timestamp>(accumulation_ms_) * 1000;
    if (t_max_ - last_repaint_ts_ >= accum_us) {
        last_repaint_ts_ = t_max_;
        status_label_->setText(tr("%1 samples").arg(samples_.size()));
        update();
    }
}

void TemporalPlotWindow::set_mode(int mode) {
    mode_ = (mode == 1) ? Mode::YT : Mode::XT;
    update_position_range();
    clear();
}

void TemporalPlotWindow::set_time_window_ms(int ms) {
    window_ms_ = std::max(50, ms);
    if (!samples_.empty()) {
        prune_old_samples();
    }
    update();
}

void TemporalPlotWindow::clear() {
    samples_.clear();
    t_min_ = t_max_ = 0;
    last_repaint_ts_ = 0;
    status_label_->setText(tr("0 samples"));
    update();
}

// ---------------------------------------------------------------------------

void TemporalPlotWindow::on_mode_changed(int mode) {
    set_mode(mode);
}

void TemporalPlotWindow::on_position_changed(int pos) {
    axis_position_ = pos;
    // Clear on position change so stale samples from the old line don't
    // linger — the new line starts with a fresh trace.
    clear();
}

void TemporalPlotWindow::update_position_range() {
    // X-T mode: axis_position selects a Y row → range [0, height-1]
    // Y-T mode: axis_position selects an X column → range [0, width-1]
    int hi = 9999;
    if (mode_ == Mode::XT) {
        if (sensor_h_ > 0) hi = sensor_h_ - 1;
    } else {
        if (sensor_w_ > 0) hi = sensor_w_ - 1;
    }
    QSignalBlocker b(position_spin_);
    position_spin_->setRange(0, hi);
    if (axis_position_ > hi) {
        axis_position_ = hi / 2;
        position_spin_->setValue(axis_position_);
    }
}

void TemporalPlotWindow::prune_old_samples() {
    if (samples_.empty()) return;
    t_min_ = t_max_ - static_cast<Metavision::timestamp>(window_ms_) * 1000;
    auto it = std::lower_bound(samples_.begin(), samples_.end(), t_min_,
        [](const Sample& s, Metavision::timestamp t) { return s.t < t; });
    if (it != samples_.begin()) samples_.erase(samples_.begin(), it);
}

void TemporalPlotWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), Qt::black);

    const int ctrl_h = 40;
    const QRect plot_rect(20, ctrl_h, width() - 40, height() - ctrl_h - 20);
    if (plot_rect.width() <= 0 || plot_rect.height() <= 0) return;

    // Draw axes.
    p.setPen(QColor(120, 120, 120));
    p.drawRect(plot_rect.adjusted(0, 0, -1, -1));

    if (samples_.empty()) {
        p.setPen(QColor(180, 180, 180));
        p.drawText(plot_rect, Qt::AlignCenter,
                   tr("No data on %1 = %2")
                       .arg(mode_ == Mode::XT ? tr("row Y") : tr("col X"))
                       .arg(axis_position_));
        return;
    }

    // Determine coordinate range from sensor geometry (preferred) or from
    // observed samples (fallback when geometry is unknown).
    int coord_min = 0;
    int coord_max = 0;
    if (mode_ == Mode::XT) {
        coord_max = sensor_w_ > 0 ? sensor_w_ - 1 : 0;
    } else {
        coord_max = sensor_h_ > 0 ? sensor_h_ - 1 : 0;
    }
    if (coord_max <= coord_min) {
        // Fallback: use observed range.
        coord_min = std::numeric_limits<int>::max();
        coord_max = std::numeric_limits<int>::min();
        for (const auto& s : samples_) {
            coord_min = std::min(coord_min, static_cast<int>(s.coord));
            coord_max = std::max(coord_max, static_cast<int>(s.coord));
        }
        if (coord_max == coord_min) coord_max = coord_min + 1;
    }

    const Metavision::timestamp t_span = std::max<Metavision::timestamp>(1, t_max_ - t_min_);
    const int coord_span = std::max(1, coord_max - coord_min);

    // Plot samples.
    // ON (pol=1) → green, OFF (pol=0) → red; or white if polarity disabled.
    for (const auto& s : samples_) {
        const double fx = static_cast<double>(s.t - t_min_) / t_span;
        const double fy = static_cast<double>(s.coord - coord_min) / coord_span;
        const int px = plot_rect.left() + static_cast<int>(fx * plot_rect.width());
        const int py = plot_rect.bottom() - static_cast<int>(fy * plot_rect.height());
        if (show_polarity_) {
            p.setPen(s.pol ? QColor(80, 255, 80) : QColor(255, 80, 80));
        } else {
            p.setPen(QColor(200, 255, 200));
        }
        p.drawPoint(px, py);
    }

    // Axis labels.
    p.setPen(QColor(200, 200, 200));
    p.drawText(plot_rect.bottomLeft() + QPoint(0, 15),
               tr("time (us): %1..%2").arg(t_min_).arg(t_max_));
    const QString mode_label = (mode_ == Mode::XT ? tr("X-T  ") : tr("Y-T  "));
    const QString coord_label = tr("%1: 0..%2")
        .arg(mode_ == Mode::XT ? tr("X") : tr("Y")).arg(coord_max);
    p.drawText(plot_rect.topLeft() - QPoint(0, -2), mode_label + coord_label);
}

} // namespace gui
