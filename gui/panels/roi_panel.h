// gui/panels/roi_panel.h — hardware region-of-interest control (design §3.1.3).
//
// Wraps I_ROI (HAL facility): a single rectangular window with enable toggle
// and ROI/RONI mode. The display widget can drag-draw a rectangle and push
// it here via set_roi_from_drag(); conversely, applies emit roi_applied()
// so the display can render the corresponding overlay.

#ifndef GUI_PANELS_ROI_PANEL_H
#define GUI_PANELS_ROI_PANEL_H

#include <QWidget>

#include "abstract_panel.h"

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;
class QPushButton;

namespace gui {

class CameraController;

class RoiPanel : public AbstractPanel {
    Q_OBJECT
public:
    explicit RoiPanel(QWidget* parent = nullptr);

    QString panel_id() const override { return QStringLiteral("roi"); }
    QString panel_title() const override { return tr("ROI"); }
    QString panel_group() const override { return QStringLiteral("Hardware"); }

public slots:
    void on_camera_connected(CameraController* controller) override;
    void on_camera_disconnected() override;

    /// @brief Called when the user finishes a drag on the display widget.
    /// Coordinates are in sensor pixels.
    void set_roi_from_drag(int x, int y, int w, int h);

    /// @brief Sets the ROI Drag Mode checkbox state (called by MainWindow
    /// when the display toggles drag mode).
    void set_roi_drag_checked(bool checked);

    /// @brief Enables/disables the ROI Drag Mode checkbox (disabled until
    /// a camera is connected).
    void set_roi_drag_enabled(bool enabled);

    /// @brief Populates the preset combo box (moved from Camera menu — §14.5).
    void set_preset_names(const QStringList& names);

    /// @brief Enables/disables the preset controls.
    void set_presets_enabled(bool enabled);

signals:
    /// @brief Emitted after a successful apply. The display widget uses this
    /// to draw the rectangle overlay.
    void roi_applied(int x, int y, int w, int h, bool enabled);

    /// @brief Emitted when ROI Drag Mode is toggled (moved from Camera menu).
    void roi_drag_toggled(bool on);

    /// @brief Emitted when the user clicks "Apply" on a preset (moved from
    /// Camera menu). @p index is the preset index.
    void preset_apply_requested(int index);

private:
    void populate();
    bool apply();
    void toggle_enable(bool on);

    QCheckBox*    enable_check_{nullptr};
    QComboBox*    mode_combo_{nullptr};
    QSpinBox*     x_spin_{nullptr};
    QSpinBox*     y_spin_{nullptr};
    QSpinBox*     w_spin_{nullptr};
    QSpinBox*     h_spin_{nullptr};
    QLabel*       hint_label_{nullptr};
    QLabel*       max_windows_label_{nullptr};
    QPushButton*  apply_btn_{nullptr};
    QPushButton*  clear_btn_{nullptr};

    // Camera menu controls moved here (§14.5).
    QCheckBox*    drag_check_{nullptr};
    QComboBox*    preset_combo_{nullptr};
    QPushButton*  preset_apply_btn_{nullptr};

    int  sensor_w_{0};
    int  sensor_h_{0};
    bool populated_{false};
};

} // namespace gui

#endif // GUI_PANELS_ROI_PANEL_H
