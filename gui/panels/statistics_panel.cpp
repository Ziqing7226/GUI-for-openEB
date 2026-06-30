// gui/panels/statistics_panel.cpp

#include "statistics_panel.h"

#include <QFormLayout>
#include <QLabel>

namespace gui {

StatisticsPanel::StatisticsPanel(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(8, 8, 8, 8);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto mk = [this, form](const QString& title) {
        auto* lbl = new QLabel("—", this);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(title, lbl);
        return lbl;
    };

    value_rate_  = mk(tr("Event rate"));
    value_peak_  = mk(tr("Peak rate"));
    value_ratio_ = mk(tr("ON / OFF ratio"));
    value_on_    = mk(tr("ON events"));
    value_off_   = mk(tr("OFF events"));
    value_fps_   = mk(tr("Display FPS"));
    value_ts_    = mk(tr("Last timestamp"));
}

QString StatisticsPanel::format_rate(double rate_eps) {
    if (rate_eps >= 1.0e6) {
        return QStringLiteral("%1 Mev/s").arg(rate_eps / 1.0e6, 0, 'f', 2);
    }
    if (rate_eps >= 1.0e3) {
        return QStringLiteral("%1 kev/s").arg(rate_eps / 1.0e3, 0, 'f', 2);
    }
    return QStringLiteral("%1 ev/s").arg(rate_eps, 0, 'f', 0);
}

void StatisticsPanel::set_rate(double rate_eps, double peak_eps, Metavision::timestamp /*t*/) {
    value_rate_->setText(format_rate(rate_eps));
    value_peak_->setText(format_rate(peak_eps));
}

void StatisticsPanel::set_on_off(std::uint64_t on_count, std::uint64_t off_count, double on_ratio) {
    value_ratio_->setText(QStringLiteral("%1%").arg(on_ratio * 100.0, 0, 'f', 1));
    value_on_->setText(QString::number(on_count));
    value_off_->setText(QString::number(off_count));
}

void StatisticsPanel::set_fps(double fps) {
    value_fps_->setText(QStringLiteral("%1 fps").arg(fps, 0, 'f', 1));
}

void StatisticsPanel::set_timestamp(Metavision::timestamp t) {
    // SDK timestamps are in microseconds.
    value_ts_->setText(QStringLiteral("%1 s").arg(t / 1.0e6, 0, 'f', 3));
}

void StatisticsPanel::clear() {
    for (auto* lbl : {value_rate_, value_peak_, value_ratio_, value_on_, value_off_,
                      value_fps_, value_ts_}) {
        lbl->setText(tr("—"));
    }
}

} // namespace gui
