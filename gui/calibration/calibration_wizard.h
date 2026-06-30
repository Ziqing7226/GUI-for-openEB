// gui/calibration/calibration_wizard.h — intrinsic / extrinsic calibration UI.
//
// Phase 9 (design §4.5, §5.3): a tabbed dialog hosting the Intrinsic and
// Extrinsic wizards. Each wizard lets the user configure the calibration
// board, capture frames from the live event display, run calibration, and
// export the result to a YAML file.

#ifndef GUI_CALIBRATION_CALIBRATION_WIZARD_H
#define GUI_CALIBRATION_CALIBRATION_WIZARD_H

#include <QDialog>
#include <memory>

#include "algo/calibration/intrinsic.h"
#include "algo/calibration/extrinsic.h"

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

/// @brief Tabbed dialog hosting both calibration wizards.
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
    void show_extrinsic();

private slots:
    // Intrinsic tab.
    void on_intrinsic_capture();
    void on_intrinsic_run();
    void on_intrinsic_reset();
    void on_intrinsic_save();

    // Extrinsic tab.
    void on_extrinsic_capture();
    void on_extrinsic_run();
    void on_extrinsic_reset();
    void on_extrinsic_save();

private:
    void build_intrinsic_tab();
    void build_extrinsic_tab();
    void update_intrinsic_preview(const QImage& img);
    void update_extrinsic_preview_first(const QImage& img);
    void update_extrinsic_preview_second(const QImage& img);

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

    // Extrinsic controls.
    QComboBox* ex_pattern_{nullptr};
    QSpinBox*  ex_cols_{nullptr};
    QSpinBox*  ex_rows_{nullptr};
    QDoubleSpinBox* ex_square_{nullptr};
    QLabel*    ex_status_{nullptr};
    QLabel*    ex_preview_first_{nullptr};
    QLabel*    ex_preview_second_{nullptr};
    QPushButton* ex_capture_btn_{nullptr};
    QPushButton* ex_run_btn_{nullptr};
    QPushButton* ex_reset_btn_{nullptr};
    QPushButton* ex_save_btn_{nullptr};

    std::unique_ptr<gui_algo::ExtrinsicCalibration> extrinsic_;
    gui_algo::ExtrinsicResult extrinsic_result_;
    QImage ex_last_first_;
    QImage ex_last_second_;

    CameraController* camera_{nullptr};
    EventDisplayWidget* display_{nullptr};
};

} // namespace gui

#endif // GUI_CALIBRATION_CALIBRATION_WIZARD_H
