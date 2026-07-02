// gui/widgets/algo_window.cpp — generic algorithm parameter + display window.
//
// See algo_window.h for the design rationale. The parameter panel reuses the
// same widget-generation pattern as AlgorithmsPanel (auto-build QSpinBox /
// QDoubleSpinBox / QComboBox / QLineEdit from AlgoParamSpec), so any new
// algorithm parameter registered in algo_bridge.cpp automatically appears
// here without code changes.

#include "algo_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "display/event_display_widget.h"

namespace gui {

AlgoWindow::AlgoWindow(AlgoBridge* bridge, const std::string& algo_name,
                       QWidget* parent)
    : QWidget(parent, Qt::Window),
      bridge_(bridge),
      algo_name_(algo_name) {
    setWindowTitle(QString::fromStdString(algo_name));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(560, 640);

    // Look up the algo info and find/create the live instance.
    const AlgoInfo* info = bridge_ ? bridge_->find(algo_name_) : nullptr;
    if (info) {
        info_ = *info;
        setWindowTitle(QString::fromStdString(info_.display_name));
        instance_ = bridge_->find_live(algo_name_);
        if (!instance_) instance_ = bridge_->create(algo_name_);
        if (instance_) instance_->set_enabled(true);
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // Parameter panel (scrollable so large param sets remain usable).
    build_param_panel(outer);

    // Display area: defaults to a status QLabel; Standalone frame algos
    // install an EventDisplayWidget via set_display_widget() after construction.
    display_layout_ = new QVBoxLayout();
    display_layout_->setContentsMargins(0, 0, 0, 0);
    status_label_ = new QLabel(tr("Waiting for events..."), this);
    status_label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    QFont f(QStringLiteral("Monospace"));
    f.setStyleHint(QFont::TypeWriter);
    f.setPointSize(10);
    status_label_->setFont(f);
    status_label_->setMinimumHeight(120);
    display_widget_ = status_label_;
    display_layout_->addWidget(display_widget_);
    outer->addLayout(display_layout_, 1);
}

void AlgoWindow::build_param_panel(QVBoxLayout* outer) {
    // Group parameters: ROI params first (so the user can quickly toggle/resize
    // the region), then the algorithm-specific params.
    std::vector<AlgoParamSpec> roi_params, algo_params;
    for (const auto& p : info_.params) {
        if (p.key.find("roi_") == 0) roi_params.push_back(p);
        else algo_params.push_back(p);
    }

    param_scroll_ = new QScrollArea(this);
    param_scroll_->setWidgetResizable(true);
    param_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Cap the parameter panel height so the display area stays visible.
    param_scroll_->setMaximumHeight(280);

    auto* host = new QWidget(param_scroll_);
    auto* host_layout = new QVBoxLayout(host);
    host_layout->setContentsMargins(4, 4, 4, 4);
    host_layout->setSpacing(6);

    auto build_group = [&](const QString& title,
                           const std::vector<AlgoParamSpec>& params) {
        if (params.empty()) return;
        auto* gb = new QGroupBox(title, host);
        auto* form = new QFormLayout(gb);
        form->setContentsMargins(6, 6, 6, 6);

        for (const auto& p : params) {
            const QString disp = QString::fromStdString(p.display_name);
            const std::string param_key = p.key;
            QWidget* w = nullptr;
            if (p.type == "enum") {
                auto* cmb = new QComboBox(gb);
                for (const auto& v : p.enum_values) cmb->addItem(QString::fromStdString(v));
                w = cmb;
                connect(cmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this, param_key, cmb](int) {
                            apply_param(param_key, cmb->currentText().toStdString());
                        });
            } else if (p.type == "bool") {
                auto* cmb = new QComboBox(gb);
                cmb->addItem("false"); cmb->addItem("true");
                w = cmb;
                // Sync initial value from the live instance (if any).
                if (instance_) {
                    const std::string v = instance_->get_param(param_key);
                    cmb->setCurrentIndex(v == "true" || v == "1" ? 1 : 0);
                }
                connect(cmb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this, param_key, cmb](int) {
                            apply_param(param_key, cmb->currentText().toStdString());
                        });
            } else if (p.type == "int") {
                auto* sp = new QSpinBox(gb);
                bool oklo = false, okhi = false;
                int lo = QString::fromStdString(p.min_value).toInt(&oklo);
                int hi = QString::fromStdString(p.max_value).toInt(&okhi);
                sp->setRange(oklo ? lo : -100000000, okhi ? hi : 100000000);
                int init_val = QString::fromStdString(p.default_value).toInt();
                if (instance_) {
                    const std::string v = instance_->get_param(param_key);
                    if (!v.empty()) init_val = QString::fromStdString(v).toInt();
                }
                sp->setValue(init_val);
                w = sp;
                connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                        [this, param_key](int v) {
                            apply_param(param_key, std::to_string(v));
                        });
            } else if (p.type == "float") {
                auto* sp = new QDoubleSpinBox(gb);
                sp->setRange(-1e9, 1e9);
                sp->setDecimals(6);
                double init_val = QString::fromStdString(p.default_value).toDouble();
                if (instance_) {
                    const std::string v = instance_->get_param(param_key);
                    if (!v.empty()) init_val = QString::fromStdString(v).toDouble();
                }
                sp->setValue(init_val);
                w = sp;
                connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                        [this, param_key](double v) {
                            apply_param(param_key, std::to_string(v));
                        });
            } else {
                auto* le = new QLineEdit(QString::fromStdString(p.default_value), gb);
                if (instance_) {
                    const std::string v = instance_->get_param(param_key);
                    if (!v.empty()) le->setText(QString::fromStdString(v));
                }
                w = le;
                connect(le, &QLineEdit::textChanged, this,
                        [this, param_key](const QString& v) {
                            apply_param(param_key, v.toStdString());
                        });
            }
            form->addRow(disp, w);
        }
        host_layout->addWidget(gb);
    };

    build_group(tr("Algorithm ROI"), roi_params);
    build_group(tr("Parameters"), algo_params);

    host_layout->addStretch(1);
    param_scroll_->setWidget(host);
    outer->addWidget(param_scroll_);
}

void AlgoWindow::apply_param(const std::string& key, const std::string& value) {
    if (!instance_) return;
    instance_->set_param(key, value);
}

EventDisplayWidget* AlgoWindow::frame_display() const {
    return qobject_cast<EventDisplayWidget*>(display_widget_);
}

void AlgoWindow::set_display_widget(QWidget* w) {
    if (!w || w == display_widget_) return;
    // Remove the old display widget (default status label) and install the
    // new one in the same layout slot.
    if (display_widget_) {
        display_layout_->removeWidget(display_widget_);
        delete display_widget_;
    }
    display_widget_ = w;
    display_layout_->addWidget(display_widget_, 1);
    // The status label is gone after a custom widget is installed.
    status_label_ = nullptr;
}

void AlgoWindow::set_status_text(const QString& text) {
    if (status_label_) {
        status_label_->setText(text);
    }
}

void AlgoWindow::closeEvent(QCloseEvent* event) {
    emit closing(algo_name_);
    QWidget::closeEvent(event);
}

} // namespace gui
