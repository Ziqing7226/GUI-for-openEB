// gui/calibration/calibration_wizard.cpp

#include "calibration_wizard.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

#include "app/camera_controller.h"
#include "display/event_display_widget.h"

namespace gui {

namespace {

// Casts a QImage to an OpenCV BGR Mat (deep copy).
cv::Mat qimage_to_bgr(const QImage& img) {
    if (img.isNull()) return cv::Mat();
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

} // namespace

CalibrationWizard::CalibrationWizard(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Calibration Wizard"));
    setMinimumSize(700, 520);

    intrinsic_ = std::make_unique<gui_algo::IntrinsicCalibration>();

    tabs_ = new QTabWidget(this);
    build_intrinsic_tab();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs_);

    auto* close_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close_box);
}

CalibrationWizard::~CalibrationWizard() = default;

void CalibrationWizard::set_camera(CameraController* controller) {
    camera_ = controller;
    const bool has_camera = (camera_ != nullptr && camera_->is_connected());
    in_capture_btn_->setEnabled(has_camera);
}

void CalibrationWizard::set_display(EventDisplayWidget* display) {
    display_ = display;
}

void CalibrationWizard::show_intrinsic() {
    tabs_->setCurrentIndex(0);
    show();
    raise();
    activateWindow();
}

// ---------------------------------------------------------------------------
// Intrinsic tab
// ---------------------------------------------------------------------------

void CalibrationWizard::build_intrinsic_tab() {
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);

    // Configuration form.
    auto* form = new QFormLayout();
    in_pattern_ = new QComboBox(page);
    in_pattern_->addItem(tr("Chessboard"), static_cast<int>(gui_algo::CalibrationPattern::Chessboard));
    in_pattern_->addItem(tr("Circle Grid"), static_cast<int>(gui_algo::CalibrationPattern::CircleGrid));
    in_pattern_->addItem(tr("Asymmetric Circles"), static_cast<int>(gui_algo::CalibrationPattern::AsymmetricCircles));
    form->addRow(tr("Pattern"), in_pattern_);

    in_cols_ = new QSpinBox(page);
    in_cols_->setRange(2, 30);
    in_cols_->setValue(9);
    in_rows_ = new QSpinBox(page);
    in_rows_->setRange(2, 30);
    in_rows_->setValue(6);
    auto* dims_row = new QWidget(page);
    auto* dims_layout = new QHBoxLayout(dims_row);
    dims_layout->setContentsMargins(0, 0, 0, 0);
    dims_layout->addWidget(new QLabel(tr("Cols:"), dims_row));
    dims_layout->addWidget(in_cols_);
    dims_layout->addWidget(new QLabel(tr("Rows:"), dims_row));
    dims_layout->addWidget(in_rows_);
    dims_layout->addStretch();
    form->addRow(tr("Board dims"), dims_row);

    in_square_ = new QDoubleSpinBox(page);
    in_square_->setRange(0.1, 100.0);
    in_square_->setValue(25.0);
    in_square_->setSuffix(" mm");
    form->addRow(tr("Square size"), in_square_);

    outer->addLayout(form);

    // Buttons.
    auto* btn_row = new QHBoxLayout();
    in_capture_btn_ = new QPushButton(tr("Capture Frame"), page);
    in_run_btn_ = new QPushButton(tr("Run Calibration"), page);
    in_reset_btn_ = new QPushButton(tr("Reset"), page);
    in_save_btn_ = new QPushButton(tr("Save Result..."), page);
    in_save_btn_->setEnabled(false);
    btn_row->addWidget(in_capture_btn_);
    btn_row->addWidget(in_run_btn_);
    btn_row->addWidget(in_reset_btn_);
    btn_row->addWidget(in_save_btn_);
    btn_row->addStretch();
    outer->addLayout(btn_row);

    // Preview area.
    in_preview_area_ = new QScrollArea(page);
    in_preview_area_->setAlignment(Qt::AlignCenter);
    in_preview_label_ = new QLabel(in_preview_area_);
    in_preview_label_->setText(tr("No frames captured yet."));
    in_preview_area_->setWidget(in_preview_label_);
    in_preview_area_->setMinimumHeight(220);
    outer->addWidget(in_preview_area_, 1);

    in_status_ = new QLabel(tr("Captured: 0 frames"), page);
    outer->addWidget(in_status_);

    connect(in_capture_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_intrinsic_capture);
    connect(in_run_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_intrinsic_run);
    connect(in_reset_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_intrinsic_reset);
    connect(in_save_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_intrinsic_save);

    // Apply initial configuration.
    on_intrinsic_reset();

    tabs_->addTab(page, tr("Intrinsic"));
}

void CalibrationWizard::on_intrinsic_capture() {
    if (!camera_ || !camera_->is_connected()) {
        QMessageBox::warning(this, tr("Calibration"), tr("No camera connected."));
        return;
    }
    // Refresh the detector config from the current controls so the user can
    // change pattern/dims/square and capture without first clicking Reset/Run.
    intrinsic_->set_pattern(
        static_cast<gui_algo::CalibrationPattern>(in_pattern_->currentData().toInt()),
        in_cols_->value(), in_rows_->value(),
        static_cast<float>(in_square_->value()));
    auto* display = display_;
    if (!display) {
        QMessageBox::warning(this, tr("Calibration"), tr("Cannot access the event display."));
        return;
    }
    const QImage qimg = display->current_frame();
    if (qimg.isNull()) {
        in_status_->setText(tr("No frame available yet. Wait for events."));
        return;
    }
    cv::Mat bgr = qimage_to_bgr(qimg);
    auto res = intrinsic_->add_frame(bgr, true);
    if (res.found) {
        in_last_preview_ = cv_to_qimage(res.image);
        update_intrinsic_preview(in_last_preview_);
        in_status_->setText(tr("Captured: %1 frames (last detection OK)")
            .arg(intrinsic_->frame_count()));
    } else {
        in_status_->setText(tr("Pattern not found in current frame. Captured: %1.")
            .arg(intrinsic_->frame_count()));
    }
}

void CalibrationWizard::on_intrinsic_run() {
    intrinsic_->set_pattern(
        static_cast<gui_algo::CalibrationPattern>(in_pattern_->currentData().toInt()),
        in_cols_->value(), in_rows_->value(),
        static_cast<float>(in_square_->value()));
    in_status_->setText(tr("Running calibration..."));
    QApplication::processEvents();
    intrinsic_result_ = intrinsic_->run();
    if (intrinsic_result_.ok) {
        in_status_->setText(tr("Calibration OK. RMS = %1 px (%2 frames).")
            .arg(intrinsic_result_.rms, 0, 'f', 3)
            .arg(intrinsic_result_.frames_used));
        in_save_btn_->setEnabled(true);
    } else {
        QMessageBox::warning(this, tr("Calibration failed"),
            QString::fromStdString(intrinsic_result_.error));
        in_status_->setText(tr("Calibration failed: %1")
            .arg(QString::fromStdString(intrinsic_result_.error)));
    }
}

void CalibrationWizard::on_intrinsic_reset() {
    intrinsic_->set_pattern(
        static_cast<gui_algo::CalibrationPattern>(in_pattern_->currentData().toInt()),
        in_cols_->value(), in_rows_->value(),
        static_cast<float>(in_square_->value()));
    intrinsic_->reset();
    intrinsic_result_ = {};
    in_last_preview_ = QImage();
    in_preview_label_->clear();
    in_preview_label_->setText(tr("No frames captured yet."));
    in_status_->setText(tr("Captured: 0 frames"));
    in_save_btn_->setEnabled(false);
}

void CalibrationWizard::on_intrinsic_save() {
    if (!intrinsic_result_.ok) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Intrinsic Calibration"),
        QStringLiteral("intrinsic_%1.yml").arg(
            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        tr("YAML (*.yml *.yaml);;All Files (*)"));
    if (path.isEmpty()) return;
    try {
        cv::FileStorage fs(path.toStdString(), cv::FileStorage::WRITE);
        if (!fs.isOpened()) throw std::runtime_error("Cannot open file");
        fs << "image_width"  << intrinsic_->image_size().width;
        fs << "image_height" << intrinsic_->image_size().height;
        fs << "camera_matrix" << intrinsic_result_.K;
        fs << "distortion_coefficients" << intrinsic_result_.dist_coeffs;
        fs << "rms" << intrinsic_result_.rms;
        fs.release();
        in_status_->setText(tr("Saved to %1").arg(path));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Save failed"), e.what());
    }
}

void CalibrationWizard::update_intrinsic_preview(const QImage& img) {
    if (img.isNull()) return;
    in_preview_label_->setPixmap(QPixmap::fromImage(img).scaledToWidth(
        in_preview_area_->viewport()->width(), Qt::SmoothTransformation));
    in_preview_label_->resize(in_preview_label_->pixmap().size());
}

QImage CalibrationWizard::cv_to_qimage(const cv::Mat& mat) {
    if (mat.empty()) return QImage();
    cv::Mat rgb;
    if (mat.channels() == 1) {
        cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);
    } else {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    }
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

} // namespace gui
