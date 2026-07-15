// gui/exporter/export_dialog.h — modal dialog for configuring an export job.

#ifndef GUI_EXPORTER_EXPORT_DIALOG_H
#define GUI_EXPORTER_EXPORT_DIALOG_H

#include <QDialog>
#include "exporter_controller.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QProgressBar;
class QPushButton;
class QLabel;

namespace gui {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(ExporterController* controller, QWidget* parent = nullptr);

    /// @brief Pre-fills the source path (e.g. the currently open file).
    void set_source(const QString& path);

private slots:
    void on_browse_source();
    void on_browse_output();
    void on_format_changed(int idx);
    void on_start();
    void on_completed(const QString& out);
    void on_failed(const QString& msg);
    void on_progress(double r);

private:
    ExporterController* controller_;
    QLineEdit* edt_source_{nullptr};
    QLineEdit* edt_output_{nullptr};
    QComboBox* cmb_format_{nullptr};
    QSpinBox* spn_fps_{nullptr};
    QSpinBox* spn_accum_{nullptr};
    QSpinBox* spn_quality_{nullptr};
    QCheckBox* chk_color_{nullptr};
    QProgressBar* progress_{nullptr};
    QPushButton* btn_start_{nullptr};
    QPushButton* btn_cancel_{nullptr};
    QLabel* lbl_status_{nullptr};
};

} // namespace gui

#endif // GUI_EXPORTER_EXPORT_DIALOG_H
