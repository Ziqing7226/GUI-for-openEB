// gui/calibration/calibration_wizard.h — intrinsic calibration UI.
//
// Phase 9 (design §4.5, §5.3): a dialog hosting the Intrinsic wizard.
// The wizard lets the user configure the calibration board, capture frames
// from the live event display, run calibration, and export the result to a
// YAML file.

#ifndef GUI_CALIBRATION_CALIBRATION_WIZARD_H
#define GUI_CALIBRATION_CALIBRATION_WIZARD_H

#include <QDialog>
#include <memory>

#include "algo/calibration/intrinsic.h"

class QTabWidget;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QImage;
class QScrollArea;

namespace gui {

class CameraController;
class EventDisplayWidget;

/// @brief Dialog hosting the intrinsic calibration wizard.
class CalibrationWizard : public QDialog {
    Q_OBJECT
public:
    explicit CalibrationWizard(QWidget* parent = nullptr);
    ~CalibrationWizard();

    /// @brief Provides the live camera so the wizard can pull frames.
    /// Safe to call with nullptr (capture buttons disabled until set).
    void set_camera(CameraController* controller);

    /// @brief Provides the event display to capture frames from.
    /// Required because parentWidget()->findChild<EventDisplayWidget*>()
    /// may return an MDI sub-window display instead of the main one.
    void set_display(EventDisplayWidget* display);

public slots:
    void show_intrinsic();

private slots:
    // Intrinsic tab.
    void on_intrinsic_capture();
    void on_intrinsic_run();
    void on_intrinsic_reset();
    void on_intrinsic_save();

private:
    void build_intrinsic_tab();
    void update_intrinsic_preview(const QImage& img);

    static QImage cv_to_qimage(const cv::Mat& mat);

    // Shared.
    QTabWidget* tabs_{nullptr};

    // Intrinsic controls.
    QComboBox* in_pattern_{nullptr};
    QSpinBox*  in_cols_{nullptr};
    QSpinBox*  in_rows_{nullptr};
    QDoubleSpinBox* in_square_{nullptr};
    QLabel*    in_status_{nullptr};
    QLabel*    in_preview_label_{nullptr};
    QScrollArea* in_preview_area_{nullptr};
    QPushButton* in_capture_btn_{nullptr};
    QPushButton* in_run_btn_{nullptr};
    QPushButton* in_reset_btn_{nullptr};
    QPushButton* in_save_btn_{nullptr};

    std::unique_ptr<gui_algo::IntrinsicCalibration> intrinsic_;
    gui_algo::IntrinsicResult intrinsic_result_;
    QImage in_last_preview_;

    CameraController* camera_{nullptr};
    EventDisplayWidget* display_{nullptr};
};

} // namespace gui

#endif // GUI_CALIBRATION_CALIBRATION_WIZARD_H
