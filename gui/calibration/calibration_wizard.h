// gui/calibration/calibration_wizard.h — intrinsic calibration UI.
//
// Phase 9 (design §4.5, §5.3), rebuilt around a flashing on-screen chessboard
// + auto-capture workflow suited to event cameras: the chessboard inverts at
// 20 Hz, each flip generates a burst of events along the edges, and a 1 ms
// accumulation window aligned with the flip captures the pattern "for free".
// The wizard picks the max-event 1 ms window out of every 50 ms cycle,
// detects the chessboard, rejects duplicates via MSE, and auto-ends after a
// configurable frame count (default 30) — then runs cv::calibrateCamera and
// exports the result to YAML.

#ifndef GUI_CALIBRATION_CALIBRATION_WIZARD_H
#define GUI_CALIBRATION_CALIBRATION_WIZARD_H

#include <QDialog>
#include <QImage>
#include <QPointer>
#include <memory>

#include "algo/calibration/intrinsic.h"
#include "calibration_event_tap.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace gui {

class CameraController;
class ChessboardDisplay;
class EventDisplayWidget;

/// @brief Dialog hosting the intrinsic calibration wizard.
class CalibrationWizard : public QDialog {
    Q_OBJECT
public:
    explicit CalibrationWizard(QWidget* parent = nullptr);
    ~CalibrationWizard();

    /// @brief Provides the live camera so the wizard can tap CD events.
    /// Safe to call with nullptr (capture buttons disabled until set).
    void set_camera(CameraController* controller);

    /// @brief Provides the event display (unused by the auto-capture loop
    /// but kept for the manual fallback and for sensor geometry lookup).
    void set_display(EventDisplayWidget* display);

public slots:
    void show_intrinsic();

private slots:
    // Intrinsic tab.
    void on_show_chessboard();
    void on_start_capture();
    void on_stop_capture();
    void on_intrinsic_reset();
    void on_intrinsic_save();
    void on_capture_tick();

private:
    void build_intrinsic_tab();
    void update_intrinsic_preview(const QImage& img);
    void set_status(const QString& text);
    void rebuild_screen_combo();
    QString default_export_path() const;
    QImage cv_to_qimage(const cv::Mat& mat);
    /// @brief Renders a 1 ms event window to a single-channel cv::Mat at
    ///        sensor resolution (background grey, ON events white, OFF black).
    cv::Mat render_event_window(const std::vector<Metavision::EventCD>& evs,
                                int sensor_w, int sensor_h);
    /// @brief Mean-squared-error between two grayscale Mats of equal size.
    static double mse_gray(const cv::Mat& a, const cv::Mat& b);

    // Shared.
    QTabWidget* tabs_{nullptr};

    // Intrinsic controls.
    QComboBox*  in_screen_{nullptr};
    QSpinBox*   in_cols_{nullptr};
    QSpinBox*   in_rows_{nullptr};
    QSpinBox*   in_target_frames_{nullptr};
    QDoubleSpinBox* in_mse_threshold_{nullptr};
    QLabel*     in_status_{nullptr};
    QLabel*     in_preview_label_{nullptr};
    QScrollArea* in_preview_area_{nullptr};
    QProgressBar* in_progress_{nullptr};
    QPushButton* in_show_board_btn_{nullptr};
    QPushButton* in_start_btn_{nullptr};
    QPushButton* in_stop_btn_{nullptr};
    QPushButton* in_reset_btn_{nullptr};
    QPushButton* in_save_btn_{nullptr};

    std::unique_ptr<gui_algo::IntrinsicCalibration> intrinsic_;
    gui_algo::IntrinsicResult intrinsic_result_;
    QImage in_last_preview_;

    // Auto-capture state.
    CalibrationEventTap tap_;
    QTimer* capture_timer_{nullptr};
    bool capturing_{false};
    cv::Mat last_accepted_gray_;  ///< For MSE duplicate check.

    CameraController* camera_{nullptr};
    QPointer<EventDisplayWidget> display_;
    QPointer<ChessboardDisplay> chessboard_;
};

} // namespace gui

#endif // GUI_CALIBRATION_CALIBRATION_WIZARD_H
