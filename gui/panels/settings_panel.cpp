// gui/panels/settings_panel.cpp

#include "settings_panel.h"

#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
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

    // 两个页面：基础功能 + 算法模块
    tabs_ = new QTabWidget(this);
    tabs_->setTabPosition(QTabWidget::North);
    outer->addWidget(tabs_);

    // --- Tab 1: 基础功能 (Basic Features) ---
    auto* basic_scroll = new QScrollArea(tabs_);
    basic_scroll->setWidgetResizable(true);
    basic_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* basic_host = new QWidget(basic_scroll);
    auto* basic_layout = new QVBoxLayout(basic_host);
    basic_layout->setContentsMargins(6, 6, 6, 6);
    basic_layout->setSpacing(8);

    auto add_group = [&](const QString& title, QWidget* body, bool enabled = true) {
        auto* gb = new QGroupBox(title, basic_host);
        auto* gl = new QVBoxLayout(gb);
        gl->setContentsMargins(6, 6, 6, 6);
        gl->addWidget(body);
        gb->setCheckable(false);
        gb->setEnabled(enabled);
        basic_layout->addWidget(gb);
        return gb;
    };

    auto add_placeholder = [&](const QString& title, const QString& note) {
        auto* gb = new QGroupBox(title, basic_host);
        auto* gl = new QVBoxLayout(gb);
        gl->setContentsMargins(6, 6, 6, 6);
        auto* lbl = new QLabel(note, gb);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("color: #888; font-style: italic;");
        gl->addWidget(lbl);
        gb->setEnabled(false);
        basic_layout->addWidget(gb);
        return gb;
    };

    devices_ = new DevicesPanel(basic_host);
    add_group(tr("Devices"), devices_);

    information_ = new InformationPanel(basic_host);
    add_group(tr("Information"), information_);

    statistics_ = new StatisticsPanel(basic_host);
    add_group(tr("Statistics"), statistics_);

    display_ = new DisplayPanel(basic_host);
    add_group(tr("Display"), display_);

    // Phase 2: camera control panels (Bias / ROI / ESP / Trigger).
    biases_panel_  = new BiasesPanel(basic_host);
    add_group(tr("Biases"), biases_panel_);
    roi_     = new RoiPanel(basic_host);
    add_group(tr("ROI"), roi_);
    esp_     = new EspPanel(basic_host);
    add_group(tr("ESP"), esp_);
    trigger_ = new TriggerPanel(basic_host);
    add_group(tr("Trigger"), trigger_);

    // Phase 5: event preprocessing.
    preprocessing_ = new PreprocessingPanel(basic_host);
    add_group(tr("Preprocessing"), preprocessing_);

    file_tools_ = new FileToolsPanel(converter, basic_host);
    add_group(tr("File Tools"), file_tools_, converter != nullptr);

    // Calibration — placeholder until CalibrationWizard is installed.
    calibration_group_ = add_placeholder(tr("Calibration"),
                                         tr("Open via menu: Calibration → Intrinsic Wizard..."));

    basic_layout->addStretch(1);
    basic_scroll->setWidget(basic_host);
    tabs_->addTab(basic_scroll, tr("基础功能"));

    // --- Tab 2: 算法模块 (Algorithm Modules) ---
    // AlgorithmsPanel already contains the global Algorithm ROI selector at
    // its top, followed by the scrollable algorithm list. All algorithm
    // configuration lives here — the Algorithm menu bar is removed from
    // MainWindow to avoid duplication.
    auto* algo_scroll = new QScrollArea(tabs_);
    algo_scroll->setWidgetResizable(true);
    algo_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    algorithms_ = new AlgorithmsPanel(bridge, algo_scroll);
    algo_scroll->setWidget(algorithms_);
    tabs_->addTab(algo_scroll, tr("算法模块"));

    // Default to the basic tab.
    tabs_->setCurrentIndex(0);
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
