// gui/panels/settings_panel.cpp

#include "settings_panel.h"

#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "algorithms_panel.h"
#include "biases_panel.h"
#include "devices_panel.h"
#include "display_panel.h"
#include "esp_panel.h"
#include "file_tools_panel.h"
#include "information_panel.h"
#include "preprocessing_panel.h"
#include "roi_panel.h"
#include "statistics_panel.h"
#include "trigger_panel.h"
#include "algo_bridge/algo_bridge.h"
#include "app/file_converter.h"

namespace gui {

SettingsPanel::SettingsPanel(AlgoBridge* bridge, FileConverter* converter, QWidget* parent)
    : QWidget(parent) {
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
    biases_panel_  = new BiasesPanel(host);
    add_group(tr("Biases"), biases_panel_);
    roi_     = new RoiPanel(host);
    add_group(tr("ROI"), roi_);
    esp_     = new EspPanel(host);
    add_group(tr("ESP"), esp_);
    trigger_ = new TriggerPanel(host);
    add_group(tr("Trigger"), trigger_);

    // Phase 5: event preprocessing & algorithm selection.
    preprocessing_ = new PreprocessingPanel(host);
    add_group(tr("Preprocessing"), preprocessing_);

    algorithms_ = new AlgorithmsPanel(bridge, host);
    add_group(tr("Algorithms"), algorithms_, bridge != nullptr);

    file_tools_ = new FileToolsPanel(converter, host);
    add_group(tr("File Tools"), file_tools_, converter != nullptr);

    // Phase 9: calibration — placeholder until CalibrationWizard is installed.
    calibration_group_ = add_placeholder(tr("Calibration"),
                                         tr("Intrinsic / extrinsic calibration wizards — Phase 9."));

    layout->addStretch(1);
    scroll->setWidget(host);
}

void SettingsPanel::set_calibration_panel(QWidget* panel) {
    if (!panel || !calibration_group_) return;
    if (panel == calibration_installed_) return;
    // Remove the previously-installed panel if any; otherwise remove the
    // placeholder label. Using findChildren with FindDirectChildrenOnly
    // avoids recursively deleting QLabels inside an already-installed panel
    // (which the previous implementation did, corrupting the UI on the
    // second call).
    if (calibration_installed_) {
        calibration_installed_->deleteLater();
        calibration_installed_ = nullptr;
    } else {
        const auto old_lbls = calibration_group_->findChildren<QLabel*>(
            QString(), Qt::FindDirectChildrenOnly);
        for (auto* l : old_lbls) l->deleteLater();
    }
    auto* gl = qobject_cast<QVBoxLayout*>(calibration_group_->layout());
    if (gl) {
        gl->addWidget(panel);
    } else {
        auto* ngl = new QVBoxLayout(calibration_group_);
        ngl->setContentsMargins(6, 6, 6, 6);
        ngl->addWidget(panel);
    }
    calibration_group_->setEnabled(true);
    calibration_installed_ = panel;
}

} // namespace gui
