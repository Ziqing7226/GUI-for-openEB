// gui/panels/settings_panel.cpp

#include "settings_panel.h"

#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "biases_panel.h"
#include "devices_panel.h"
#include "display_panel.h"
#include "esp_panel.h"
#include "information_panel.h"
#include "roi_panel.h"
#include "statistics_panel.h"
#include "trigger_panel.h"

namespace gui {

SettingsPanel::SettingsPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* host = new QWidget(scroll);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    auto add_group = [&](const QString& title, QWidget* body, bool enabled = true) {
        auto* gb = new QGroupBox(title, host);
        auto* gl = new QVBoxLayout(gb);
        gl->setContentsMargins(6, 6, 6, 6);
        gl->addWidget(body);
        gb->setCheckable(false);
        gb->setEnabled(enabled);
        layout->addWidget(gb);
        return gb;
    };

    auto add_placeholder = [&](const QString& title, const QString& note) {
        auto* gb = new QGroupBox(title, host);
        auto* gl = new QVBoxLayout(gb);
        gl->setContentsMargins(6, 6, 6, 6);
        auto* lbl = new QLabel(note, gb);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("color: #888; font-style: italic;");
        gl->addWidget(lbl);
        gb->setEnabled(false);
        layout->addWidget(gb);
        return gb;
    };

    devices_ = new DevicesPanel(host);
    add_group(tr("Devices"), devices_);

    information_ = new InformationPanel(host);
    add_group(tr("Information"), information_);

    statistics_ = new StatisticsPanel(host);
    add_group(tr("Statistics"), statistics_);

    display_ = new DisplayPanel(host);
    add_group(tr("Display"), display_);

    // Phase 2: camera control panels (Bias / ROI / ESP / Trigger).
    biases_  = new BiasesPanel(host);
    add_group(tr("Biases"), biases_);
    roi_     = new RoiPanel(host);
    add_group(tr("ROI"), roi_);
    esp_     = new EspPanel(host);
    add_group(tr("ESP"), esp_);
    trigger_ = new TriggerPanel(host);
    add_group(tr("Trigger"), trigger_);

    // Phase 5+: algorithm / file-tool panels.
    add_placeholder(tr("Preprocessing"), tr("OpenEB event filters & preprocessors — Phase 5."));
    add_placeholder(tr("Algorithms"),    tr("Self-developed CV / analytics / calibration — Phases 6-9."));
    add_placeholder(tr("Calibration"),   tr("Intrinsic / extrinsic calibration wizards — Phase 9."));
    add_placeholder(tr("File Tools"),    tr("RAW → HDF5 / CSV / DAT, cutter, info — Phase 5."));

    layout->addStretch(1);
    scroll->setWidget(host);
}

} // namespace gui
