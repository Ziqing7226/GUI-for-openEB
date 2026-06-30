// gui/panels/settings_panel.h — right-dock container aggregating all sub-panels.
//
// Active in Phase 1: Devices, Information, Statistics, Display.
// Phase 2+ sections (Biases, ROI, ESP, Trigger, Preprocessing, Algorithms,
// Calibration, File Tools) are present as disabled placeholder group boxes so
// the full UI structure is visible per design §5.2.

#ifndef GUI_PANELS_SETTINGS_PANEL_H
#define GUI_PANELS_SETTINGS_PANEL_H

#include <QWidget>

namespace gui {

class InformationPanel;
class StatisticsPanel;
class DisplayPanel;
class DevicesPanel;
class BiasesPanel;
class RoiPanel;
class EspPanel;
class TriggerPanel;

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget* parent = nullptr);

    InformationPanel* information_panel() const { return information_; }
    StatisticsPanel* statistics_panel() const { return statistics_; }
    DisplayPanel*    display_panel()    const { return display_; }
    DevicesPanel*    devices_panel()    const { return devices_; }
    BiasesPanel*     biases_panel()     const { return biases_; }
    RoiPanel*        roi_panel()        const { return roi_; }
    EspPanel*        esp_panel()        const { return esp_; }
    TriggerPanel*    trigger_panel()    const { return trigger_; }

private:
    InformationPanel* information_{nullptr};
    StatisticsPanel* statistics_{nullptr};
    DisplayPanel*    display_{nullptr};
    DevicesPanel*    devices_{nullptr};
    BiasesPanel*     biases_{nullptr};
    RoiPanel*        roi_{nullptr};
    EspPanel*        esp_{nullptr};
    TriggerPanel*    trigger_{nullptr};
};

} // namespace gui

#endif // GUI_PANELS_SETTINGS_PANEL_H
