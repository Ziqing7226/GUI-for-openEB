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
    extrinsic_ = std::make_unique<gui_algo::ExtrinsicCalibration>();

    tabs_ = new QTabWidget(this);
    build_intrinsic_tab();
    build_extrinsic_tab();

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
    ex_capture_btn_->setEnabled(has_camera);
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

void CalibrationWizard::show_extrinsic() {
    tabs_->setCurrentIndex(1);
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

// ---------------------------------------------------------------------------
// Extrinsic tab
// ---------------------------------------------------------------------------

void CalibrationWizard::build_extrinsic_tab() {
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);

    auto* form = new QFormLayout();
    ex_pattern_ = new QComboBox(page);
    ex_pattern_->addItem(tr("Chessboard"), static_cast<int>(gui_algo::CalibrationPattern::Chessboard));
    ex_pattern_->addItem(tr("Circle Grid"), static_cast<int>(gui_algo::CalibrationPattern::CircleGrid));
    ex_pattern_->addItem(tr("Asymmetric Circles"), static_cast<int>(gui_algo::CalibrationPattern::AsymmetricCircles));
    form->addRow(tr("Pattern"), ex_pattern_);

    ex_cols_ = new QSpinBox(page);
    ex_cols_->setRange(2, 30);
    ex_cols_->setValue(9);
    ex_rows_ = new QSpinBox(page);
    ex_rows_->setRange(2, 30);
    ex_rows_->setValue(6);
    auto* dims_row = new QWidget(page);
    auto* dims_layout = new QHBoxLayout(dims_row);
    dims_layout->setContentsMargins(0, 0, 0, 0);
    dims_layout->addWidget(new QLabel(tr("Cols:"), dims_row));
    dims_layout->addWidget(ex_cols_);
    dims_layout->addWidget(new QLabel(tr("Rows:"), dims_row));
    dims_layout->addWidget(ex_rows_);
    dims_layout->addStretch();
    form->addRow(tr("Board dims"), dims_row);

    ex_square_ = new QDoubleSpinBox(page);
    ex_square_->setRange(0.1, 100.0);
    ex_square_->setValue(25.0);
    ex_square_->setSuffix(" mm");
    form->addRow(tr("Square size"), ex_square_);

    outer->addLayout(form);

    auto* btn_row = new QHBoxLayout();
    ex_capture_btn_ = new QPushButton(tr("Capture Pair"), page);
    ex_run_btn_ = new QPushButton(tr("Run Stereo Calib"), page);
    ex_reset_btn_ = new QPushButton(tr("Reset"), page);
    ex_save_btn_ = new QPushButton(tr("Save Result..."), page);
    ex_save_btn_->setEnabled(false);
    btn_row->addWidget(ex_capture_btn_);
    btn_row->addWidget(ex_run_btn_);
    btn_row->addWidget(ex_reset_btn_);
    btn_row->addWidget(ex_save_btn_);
    btn_row->addStretch();
    outer->addLayout(btn_row);

    // Two side-by-side previews.
    auto* preview_row = new QHBoxLayout();
    ex_preview_first_ = new QLabel(page);
    ex_preview_first_->setText(tr("Camera 1 preview"));
    ex_preview_first_->setAlignment(Qt::AlignCenter);
    ex_preview_first_->setMinimumSize(280, 200);
    ex_preview_second_ = new QLabel(page);
    ex_preview_second_->setText(tr("Camera 2 preview"));
    ex_preview_second_->setAlignment(Qt::AlignCenter);
    ex_preview_second_->setMinimumSize(280, 200);
    preview_row->addWidget(ex_preview_first_, 1);
    preview_row->addWidget(ex_preview_second_, 1);
    outer->addLayout(preview_row, 1);

    ex_status_ = new QLabel(tr("Captured: 0 pairs"), page);
    outer->addWidget(ex_status_);

    connect(ex_capture_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_extrinsic_capture);
    connect(ex_run_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_extrinsic_run);
    connect(ex_reset_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_extrinsic_reset);
    connect(ex_save_btn_, &QPushButton::clicked, this, &CalibrationWizard::on_extrinsic_save);

    on_extrinsic_reset();
    tabs_->addTab(page, tr("Extrinsic"));
}

void CalibrationWizard::on_extrinsic_capture() {
    if (!camera_ || !camera_->is_connected()) {
        QMessageBox::warning(this, tr("Calibration"), tr("No camera connected."));
        return;
    }
    // Refresh detector config from the current controls before capturing.
    extrinsic_->set_pattern(
        static_cast<gui_algo::CalibrationPattern>(ex_pattern_->currentData().toInt()),
        ex_cols_->value(), ex_rows_->value(),
        static_cast<float>(ex_square_->value()));
    // Phase 9 single-camera limitation: capture the same frame into both
    // slots (useful for verifying the pipeline). True stereo capture requires
    // multi-camera support added in a later phase.
    auto* display = display_;
    if (!display) return;
    const QImage qimg = display->current_frame();
    if (qimg.isNull()) {
        ex_status_->setText(tr("No frame available yet."));
        return;
    }
    cv::Mat bgr = qimage_to_bgr(qimg);
    auto pr = extrinsic_->add_pair(bgr, bgr, true);
    if (pr.both_found) {
        ex_last_first_ = cv_to_qimage(pr.first.image);
        ex_last_second_ = cv_to_qimage(pr.second.image);
        update_extrinsic_preview_first(ex_last_first_);
        update_extrinsic_preview_second(ex_last_second_);
        ex_status_->setText(tr("Captured: %1 pairs").arg(extrinsic_->pair_count()));
    } else {
        ex_status_->setText(tr("Pattern not visible in both. Pairs: %1.")
            .arg(extrinsic_->pair_count()));
    }
}

void CalibrationWizard::on_extrinsic_run() {
    // Without two real cameras, stereo calibration on identical frames is
    // degenerate (R=I, T=0). We still run it to validate the pipeline; users
    // with two cameras supply K1/d1/K2/d2 via the file dialog.
    const QString k1_path = QFileDialog::getOpenFileName(
        this, tr("Load Camera 1 Intrinsics (YAML)"), {}, tr("YAML (*.yml *.yaml)"));
    if (k1_path.isEmpty()) return;
    const QString k2_path = QFileDialog::getOpenFileName(
        this, tr("Load Camera 2 Intrinsics (YAML)"), {}, tr("YAML (*.yml *.yaml)"));
    if (k2_path.isEmpty()) return;

    cv::Mat K1, d1, K2, d2;
    try {
        cv::FileStorage fs1(k1_path.toStdString(), cv::FileStorage::READ);
        fs1["camera_matrix"] >> K1;
        fs1["distortion_coefficients"] >> d1;
        fs1.release();
        cv::FileStorage fs2(k2_path.toStdString(), cv::FileStorage::READ);
        fs2["camera_matrix"] >> K2;
        fs2["distortion_coefficients"] >> d2;
        fs2.release();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Load failed"), e.what());
        return;
    }
    if (K1.empty() || d1.empty() || K2.empty() || d2.empty()) {
        QMessageBox::warning(this, tr("Load failed"),
            tr("Could not read K/distortion from one or both files."));
        return;
    }

    extrinsic_->set_pattern(
        static_cast<gui_algo::CalibrationPattern>(ex_pattern_->currentData().toInt()),
        ex_cols_->value(), ex_rows_->value(),
        static_cast<float>(ex_square_->value()));
    ex_status_->setText(tr("Running stereo calibration..."));
    QApplication::processEvents();
    extrinsic_result_ = extrinsic_->run(K1, d1, K2, d2);
    if (extrinsic_result_.ok) {
        ex_status_->setText(tr("Stereo OK. RMS = %1 (%2 pairs).")
            .arg(extrinsic_result_.rms, 0, 'f', 3)
            .arg(extrinsic_result_.pairs_used));
        ex_save_btn_->setEnabled(true);
    } else {
        QMessageBox::warning(this, tr("Stereo failed"),
            QString::fromStdString(extrinsic_result_.error));
        ex_status_->setText(tr("Failed: %1").arg(QString::fromStdString(extrinsic_result_.error)));
    }
}

void CalibrationWizard::on_extrinsic_reset() {
    extrinsic_->set_pattern(
        static_cast<gui_algo::CalibrationPattern>(ex_pattern_->currentData().toInt()),
        ex_cols_->value(), ex_rows_->value(),
        static_cast<float>(ex_square_->value()));
    extrinsic_->reset();
    extrinsic_result_ = {};
    ex_last_first_ = QImage();
    ex_last_second_ = QImage();
    ex_preview_first_->clear();
    ex_preview_first_->setText(tr("Camera 1 preview"));
    ex_preview_second_->clear();
    ex_preview_second_->setText(tr("Camera 2 preview"));
    ex_status_->setText(tr("Captured: 0 pairs"));
    ex_save_btn_->setEnabled(false);
}

void CalibrationWizard::on_extrinsic_save() {
    if (!extrinsic_result_.ok) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Extrinsic Calibration"),
        QStringLiteral("extrinsic_%1.yml").arg(
            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        tr("YAML (*.yml *.yaml);;All Files (*)"));
    if (path.isEmpty()) return;
    try {
        cv::FileStorage fs(path.toStdString(), cv::FileStorage::WRITE);
        if (!fs.isOpened()) throw std::runtime_error("Cannot open file");
        fs << "R" << extrinsic_result_.R;
        fs << "T" << extrinsic_result_.T;
        fs << "E" << extrinsic_result_.E;
        fs << "F" << extrinsic_result_.F;
        fs << "rms" << extrinsic_result_.rms;
        fs.release();
        ex_status_->setText(tr("Saved to %1").arg(path));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Save failed"), e.what());
    }
}

void CalibrationWizard::update_extrinsic_preview_first(const QImage& img) {
    if (img.isNull()) return;
    ex_preview_first_->setPixmap(QPixmap::fromImage(img).scaled(
        ex_preview_first_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CalibrationWizard::update_extrinsic_preview_second(const QImage& img) {
    if (img.isNull()) return;
    ex_preview_second_->setPixmap(QPixmap::fromImage(img).scaled(
        ex_preview_second_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
