// gui/panels/settings_panel.h — right-dock container aggregating all sub-panels.
//
// Active panels:
//   Phase 1: Devices, Information, Statistics, Display
//   Phase 2: Biases, ROI, ESP, Trigger
//   Phase 5: Preprocessing, Algorithms, File Tools
//   Phase 9: Calibration (added by CalibrationWizard via set_calibration_panel)
// Disabled placeholders are kept only for sections whose underlying modules
// are still pending.

#ifndef GUI_PANELS_SETTINGS_PANEL_H
#define GUI_PANELS_SETTINGS_PANEL_H

#include <QWidget>

class QGroupBox;

namespace gui {

class InformationPanel;
class StatisticsPanel;
class DisplayPanel;
class DevicesPanel;
class BiasesPanel;
class RoiPanel;
class EspPanel;
class TriggerPanel;
class PreprocessingPanel;
class AlgorithmsPanel;
class FileToolsPanel;
class AlgoBridge;
class FileConverter;

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(AlgoBridge* bridge = nullptr,
                           FileConverter* converter = nullptr,
                           QWidget* parent = nullptr);

    InformationPanel*  information_panel()     const { return information_; }
    StatisticsPanel*   statistics_panel()      const { return statistics_; }
    DisplayPanel*      display_panel()         const { return display_; }
    DevicesPanel*      devices_panel()         const { return devices_; }
    BiasesPanel*       biases_panel()          const { return biases_panel_; }
    RoiPanel*          roi_panel()             const { return roi_; }
    EspPanel*          esp_panel()             const { return esp_; }
    TriggerPanel*      trigger_panel()         const { return trigger_; }
    PreprocessingPanel* preprocessing_panel()  const { return preprocessing_; }
    AlgorithmsPanel*   algorithms_panel()      const { return algorithms_; }
    FileToolsPanel*    file_tools_panel()      const { return file_tools_; }

    /// @brief Installs an externally-built calibration panel (Phase 9).
    void set_calibration_panel(QWidget* panel);

private:
    InformationPanel*  information_{nullptr};
    StatisticsPanel*   statistics_{nullptr};
    DisplayPanel*      display_{nullptr};
    DevicesPanel*      devices_{nullptr};
    BiasesPanel*       biases_panel_{nullptr};
    RoiPanel*          roi_{nullptr};
    EspPanel*          esp_{nullptr};
    TriggerPanel*      trigger_{nullptr};
    PreprocessingPanel* preprocessing_{nullptr};
    AlgorithmsPanel*   algorithms_{nullptr};
    FileToolsPanel*    file_tools_{nullptr};

    // Phase 9 — calibration placeholder group box and any panel installed
    // into it. Tracked by pointer so set_calibration_panel can replace the
    // placeholder cleanly without recursive findChildren() searches that
    // would delete QLabels inside an already-installed panel.
    QGroupBox* calibration_group_{nullptr};
    QWidget* calibration_installed_{nullptr};
};

} // namespace gui

#endif // GUI_PANELS_SETTINGS_PANEL_H
