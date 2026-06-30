// gui/panels/file_tools_panel.cpp

#include "file_tools_panel.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/file_converter.h"

namespace gui {

FileToolsPanel::FileToolsPanel(FileConverter* converter, QWidget* parent)
    : QWidget(parent), converter_(converter) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* gb = new QGroupBox(tr("File Tools"), this);
    auto* lay = new QVBoxLayout(gb);

    btn_hdf5_ = new QPushButton(tr("Convert to HDF5..."), gb);
    btn_csv_  = new QPushButton(tr("Convert to CSV..."), gb);
    btn_cut_  = new QPushButton(tr("File Cutter..."), gb);
    btn_info_ = new QPushButton(tr("File Info..."), gb);
    progress_ = new QProgressBar(gb);
    progress_->setRange(0, 100);
    lbl_status_ = new QLabel(tr("Ready."), gb);

    lay->addWidget(btn_hdf5_);
    lay->addWidget(btn_csv_);
    lay->addWidget(btn_cut_);
    lay->addWidget(btn_info_);
    lay->addWidget(progress_);
    lay->addWidget(lbl_status_);

    outer->addWidget(gb);

    connect(btn_hdf5_, &QPushButton::clicked, this, &FileToolsPanel::on_convert_hdf5);
    connect(btn_csv_,  &QPushButton::clicked, this, &FileToolsPanel::on_convert_csv);
    connect(btn_cut_,  &QPushButton::clicked, this, &FileToolsPanel::on_cutter);
    connect(btn_info_, &QPushButton::clicked, this, &FileToolsPanel::on_info);

    if (converter_) {
        connect(converter_, &FileConverter::completed, this, &FileToolsPanel::on_completed);
        connect(converter_, &FileConverter::failed, this, &FileToolsPanel::on_failed);
        connect(converter_, &FileConverter::progress, this, [this](double r) {
            progress_->setValue(static_cast<int>(r * 100));
        });
    }
}

void FileToolsPanel::on_convert_hdf5() {
    const QString src = QFileDialog::getOpenFileName(
        this, tr("Source file"), QString(),
        tr("Event files (*.raw *.hdf5 *.h5 *.dat);;All files (*)"));
    if (src.isEmpty()) return;
    const QString dst = QFileDialog::getSaveFileName(
        this, tr("Output HDF5"), QString(), tr("HDF5 (*.h5);;All files (*)"));
    if (dst.isEmpty()) return;
    progress_->setValue(0);
    lbl_status_->setText(tr("Converting to HDF5..."));
    btn_hdf5_->setEnabled(false);
    btn_csv_->setEnabled(false);
    btn_cut_->setEnabled(false);
    btn_info_->setEnabled(false);
    converter_->convert(src, dst, FileConverter::Format::HDF5);
}

void FileToolsPanel::on_convert_csv() {
    const QString src = QFileDialog::getOpenFileName(
        this, tr("Source file"), QString(),
        tr("Event files (*.raw *.hdf5 *.h5 *.dat);;All files (*)"));
    if (src.isEmpty()) return;
    const QString dst = QFileDialog::getSaveFileName(
        this, tr("Output CSV"), QString(), tr("CSV (*.csv);;All files (*)"));
    if (dst.isEmpty()) return;
    progress_->setValue(0);
    lbl_status_->setText(tr("Converting to CSV..."));
    btn_hdf5_->setEnabled(false);
    btn_csv_->setEnabled(false);
    btn_cut_->setEnabled(false);
    btn_info_->setEnabled(false);
    converter_->convert(src, dst, FileConverter::Format::CSV);
}

void FileToolsPanel::on_cutter() {
    const QString src = QFileDialog::getOpenFileName(
        this, tr("Source file"), QString(),
        tr("Event files (*.raw *.hdf5 *.h5 *.dat);;All files (*)"));
    if (src.isEmpty()) return;
    // Simple cut dialog: ask start/end in seconds.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("File Cutter"));
    auto* form = new QFormLayout(&dlg);
    auto* spStart = new QDoubleSpinBox(&dlg); spStart->setRange(0, 1e6); spStart->setSuffix(" s");
    auto* spEnd = new QDoubleSpinBox(&dlg); spEnd->setRange(0, 1e6); spEnd->setSuffix(" s");
    form->addRow(tr("Start:"), spStart);
    form->addRow(tr("End:"), spEnd);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    if (spEnd->value() <= spStart->value()) {
        QMessageBox::warning(this, tr("File Cutter"),
            tr("End time must be greater than start time."));
        return;
    }
    const QString dst = QFileDialog::getSaveFileName(
        this, tr("Output RAW"), QString(), tr("RAW (*.raw);;All files (*)"));
    if (dst.isEmpty()) return;
    const auto start_us = static_cast<Metavision::timestamp>(spStart->value() * 1e6);
    const auto end_us = static_cast<Metavision::timestamp>(spEnd->value() * 1e6);
    progress_->setValue(0);
    lbl_status_->setText(tr("Cutting..."));
    btn_hdf5_->setEnabled(false);
    btn_csv_->setEnabled(false);
    btn_cut_->setEnabled(false);
    btn_info_->setEnabled(false);
    converter_->cut(src, dst, start_us, end_us);
}

void FileToolsPanel::on_info() {
    const QString src = QFileDialog::getOpenFileName(
        this, tr("Source file"), QString(),
        tr("Event files (*.raw *.hdf5 *.h5 *.dat);;All files (*)"));
    if (src.isEmpty()) return;
    try {
        const auto fi = converter_->info(src);
        QMessageBox::information(this, tr("File Info"),
            tr("Path: %1\nIntegrator: %2\nSerial: %3\nPlugin: %4\nEncoding: %5\n"
               "Geometry: %6 x %7\nDuration: %8 s")
                .arg(fi.path, fi.integrator, fi.serial, fi.plugin, fi.encoding)
                .arg(fi.width).arg(fi.height)
                .arg(fi.duration_us / 1.0e6, 0, 'f', 3));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("File Info"),
            tr("Failed to read file info:\n%1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        QMessageBox::warning(this, tr("File Info"),
            tr("Failed to read file info."));
    }
}

void FileToolsPanel::on_completed(const QString& out) {
    progress_->setValue(100);
    lbl_status_->setText(tr("Done: %1").arg(out));
    btn_hdf5_->setEnabled(true);
    btn_csv_->setEnabled(true);
    btn_cut_->setEnabled(true);
    btn_info_->setEnabled(true);
}

void FileToolsPanel::on_failed(const QString& msg) {
    progress_->reset();
    lbl_status_->setText(tr("Failed: %1").arg(msg));
    btn_hdf5_->setEnabled(true);
    btn_csv_->setEnabled(true);
    btn_cut_->setEnabled(true);
    btn_info_->setEnabled(true);
}

} // namespace gui
