// gui/panels/display_panel.cpp

#include "display_panel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QStandardItemModel>

namespace gui {

DisplayPanel::DisplayPanel(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(8, 8, 8, 8);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Accumulation time: 1.0 - 1000.0 ms (design default 33.3 ms).
    auto* accum_row = new QWidget(this);
    auto* accum_layout = new QHBoxLayout(accum_row);
    accum_layout->setContentsMargins(0, 0, 0, 0);
    accum_slider_ = new QSlider(Qt::Horizontal, accum_row);
    accum_slider_->setRange(10, 10000); // 1.0 - 1000.0 ms in 0.1 ms steps
    accum_slider_->setValue(333);       // 33.3 ms
    accum_spin_ = new QDoubleSpinBox(accum_row);
    accum_spin_->setRange(1.0, 1000.0);
    accum_spin_->setSingleStep(0.1);
    accum_spin_->setSuffix(" ms");
    accum_spin_->setValue(33.3);
    accum_layout->addWidget(accum_slider_, 1);
    accum_layout->addWidget(accum_spin_, 0);
    form->addRow(tr("Accumulation"), accum_row);

    palette_combo_ = new QComboBox(this);
    palette_combo_->addItem(tr("Dark"), 0);
    palette_combo_->addItem(tr("Light"), 1);
    palette_combo_->addItem(tr("CoolWarm"), 2);
    palette_combo_->addItem(tr("Gray"), 3);
    form->addRow(tr("Color theme"), palette_combo_);

    mode_combo_ = new QComboBox(this);
    mode_combo_->addItem(tr("Diff Frame"));
    mode_combo_->addItem(tr("Integration"));
    mode_combo_->addItem(tr("Histogram"));
    mode_combo_->addItem(tr("Time Decay"));
    mode_combo_->addItem(tr("Contrast Map"));
    mode_combo_->addItem(tr("Periodic"));
    mode_combo_->addItem(tr("On-Demand"));
    // Histogram / Contrast Map / Periodic / On-Demand require algorithm
    // backends that are not yet implemented. Disable those items so only
    // feasible modes (Diff / Integration / Time Decay) are selectable,
    // matching the Frame Mode menu in MainWindow.
    for (int i : {2, 4, 5, 6}) {
        auto* model = qobject_cast<QStandardItemModel*>(mode_combo_->model());
        if (model) {
            QStandardItem* item = model->item(i);
            if (item) item->setEnabled(false);
        }
    }
    mode_combo_->setToolTip(tr("Selects event accumulation mode. "
                               "Diff / Integration / Time Decay are available; "
                               "other modes require future algorithm backends."));
    form->addRow(tr("Frame mode"), mode_combo_);
    connect(mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DisplayPanel::frame_mode_changed);

    // Wire slider <-> spinbox.
    connect(accum_slider_, &QSlider::valueChanged, this,
            [this](int v) { accum_spin_->setValue(v / 10.0); });
    connect(accum_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                QSignalBlocker b(accum_slider_);
                accum_slider_->setValue(static_cast<int>(v * 10));
            });
    connect(accum_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { emit accumulation_time_changed_us(static_cast<int>(v * 1000)); });
    connect(palette_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DisplayPanel::color_palette_changed);
}

int DisplayPanel::accumulation_time_us() const {
    return static_cast<int>(accum_spin_->value() * 1000.0);
}

int DisplayPanel::color_palette_index() const {
    return palette_combo_->currentIndex();
}

int DisplayPanel::frame_mode_index() const {
    return mode_combo_->currentIndex();
}

void DisplayPanel::set_frame_mode(int index) {
    if (index >= 0 && index < mode_combo_->count()) {
        mode_combo_->setCurrentIndex(index);
    }
}

} // namespace gui
