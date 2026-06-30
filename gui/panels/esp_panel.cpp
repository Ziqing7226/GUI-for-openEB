// gui/panels/esp_panel.cpp

#include "esp_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <metavision/hal/facilities/i_antiflicker_module.h>
#include <metavision/hal/facilities/i_erc_module.h>
#include <metavision/hal/facilities/i_event_trail_filter_module.h>

#include "app/camera_controller.h"

namespace gui {

namespace {
// Convenience: wraps a QFormLayout in a QGroupBox with a checkable title.
QGroupBox* make_group(QWidget* parent, const QString& title) {
    auto* gb = new QGroupBox(title, parent);
    auto* l = new QFormLayout(gb);
    l->setContentsMargins(8, 8, 8, 8);
    l->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return gb;
}
} // namespace

EspPanel::EspPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);

    hint_label_ = new QLabel(tr("No live camera connected."), this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet("color: #888; font-style: italic;");
    outer->addWidget(hint_label_);

    // --- Anti-Flicker -----------------------------------------------------
    af_group_ = make_group(this, tr("Anti-Flicker"));
    auto* af_form = qobject_cast<QFormLayout*>(af_group_->layout());
    af_enable_ = new QCheckBox(tr("Enable"), af_group_);
    af_form->addRow(tr("Enabled"), af_enable_);
    af_mode_ = new QComboBox(af_group_);
    af_mode_->addItem(tr("Band Stop (drop in-band)"),
                      static_cast<int>(Metavision::I_AntiFlickerModule::BAND_STOP));
    af_mode_->addItem(tr("Band Pass (keep in-band)"),
                      static_cast<int>(Metavision::I_AntiFlickerModule::BAND_PASS));
    af_form->addRow(tr("Mode"), af_mode_);
    af_preset_ = new QComboBox(af_group_);
    af_preset_->addItem(tr("Custom"));
    af_preset_->addItem(tr("50 Hz mains (band 90–110 Hz)"));
    af_preset_->addItem(tr("60 Hz mains (band 110–130 Hz)"));
    af_form->addRow(tr("Preset"), af_preset_);
    af_low_ = new QSpinBox(af_group_);
    af_low_->setRange(1, 100000);
    af_low_->setSuffix(" Hz");
    af_low_->setValue(90);
    af_high_ = new QSpinBox(af_group_);
    af_high_->setRange(1, 100000);
    af_high_->setSuffix(" Hz");
    af_high_->setValue(110);
    auto* af_band_row = new QWidget(af_group_);
    auto* af_bl = new QHBoxLayout(af_band_row);
    af_bl->setContentsMargins(0, 0, 0, 0);
    af_bl->addWidget(af_low_);
    af_bl->addWidget(new QLabel("–", af_group_));
    af_bl->addWidget(af_high_);
    af_form->addRow(tr("Frequency band"), af_band_row);
    af_duty_ = new QDoubleSpinBox(af_group_);
    af_duty_->setRange(0.0, 1.0);
    af_duty_->setSingleStep(0.05);
    af_duty_->setValue(0.5);
    af_form->addRow(tr("Duty cycle"), af_duty_);
    af_start_thr_ = new QSpinBox(af_group_);
    af_start_thr_->setRange(0, 1000000);
    af_start_thr_->setValue(1);
    af_form->addRow(tr("Start threshold"), af_start_thr_);
    af_stop_thr_ = new QSpinBox(af_group_);
    af_stop_thr_->setRange(0, 1000000);
    af_stop_thr_->setValue(1);
    af_form->addRow(tr("Stop threshold"), af_stop_thr_);
    outer->addWidget(af_group_);

    // --- Trail Filter -----------------------------------------------------
    tf_group_ = make_group(this, tr("Trail Filter"));
    auto* tf_form = qobject_cast<QFormLayout*>(tf_group_->layout());
    tf_enable_ = new QCheckBox(tr("Enable"), tf_group_);
    tf_form->addRow(tr("Enabled"), tf_enable_);
    tf_type_ = new QComboBox(tf_group_);
    tf_type_->addItem(tr("TRAIL"),
                      static_cast<int>(Metavision::I_EventTrailFilterModule::Type::TRAIL));
    tf_type_->addItem(tr("STC Cut Trail"),
                      static_cast<int>(Metavision::I_EventTrailFilterModule::Type::STC_CUT_TRAIL));
    tf_type_->addItem(tr("STC Keep Trail"),
                      static_cast<int>(Metavision::I_EventTrailFilterModule::Type::STC_KEEP_TRAIL));
    tf_form->addRow(tr("Type"), tf_type_);
    tf_threshold_ = new QSpinBox(tf_group_);
    tf_threshold_->setRange(0, 10000000);
    tf_threshold_->setSuffix(" \xC2\xB5s"); // µs
    tf_threshold_->setValue(1000);
    tf_form->addRow(tr("Threshold"), tf_threshold_);
    outer->addWidget(tf_group_);

    // --- ERC --------------------------------------------------------------
    erc_group_ = make_group(this, tr("Event Rate Controller (ERC)"));
    auto* erc_form = qobject_cast<QFormLayout*>(erc_group_->layout());
    erc_enable_ = new QCheckBox(tr("Enable"), erc_group_);
    erc_form->addRow(tr("Enabled"), erc_enable_);
    erc_rate_ = new QSpinBox(erc_group_);
    erc_rate_->setRange(1, 1000000000);
    erc_rate_->setSuffix(" ev/s");
    erc_rate_->setValue(1000000);
    erc_form->addRow(tr("Target rate"), erc_rate_);
    outer->addWidget(erc_group_);

    outer->addStretch(1);
    set_all_enabled(false);

    // --- Wire -------------------------------------------------------------
    // Anti-Flicker
    connect(af_enable_, &QCheckBox::toggled, this, [this](bool on) {
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        try { af->enable(on); } catch (const std::exception& e) {
            emit error_message(QString::fromUtf8(e.what()));
            QSignalBlocker b(af_enable_); af_enable_->setChecked(!on);
        }
    });
    connect(af_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        const auto m = static_cast<Metavision::I_AntiFlickerModule::AntiFlickerMode>(
            af_mode_->currentData().toInt());
        try { af->set_filtering_mode(m); } catch (const std::exception& e) {
            emit error_message(QString::fromUtf8(e.what()));
        }
    });
    connect(af_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx == 0) return; // Custom
        QSignalBlocker bl(af_low_);
        QSignalBlocker bh(af_high_);
        if (idx == 1) { af_low_->setValue(90);  af_high_->setValue(110); }
        else          { af_low_->setValue(110); af_high_->setValue(130); }
        // Apply.
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        try { af->set_frequency_band(af_low_->value(), af_high_->value()); }
        catch (const std::exception& e) { emit error_message(QString::fromUtf8(e.what())); }
    });
    auto apply_band = [this]() {
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        const int lo = af_low_->value();
        const int hi = af_high_->value();
        if (hi < lo) { emit error_message(tr("High frequency must be >= low.")); return; }
        try { af->set_frequency_band(lo, hi); } catch (const std::exception& e) {
            emit error_message(QString::fromUtf8(e.what()));
        }
    };
    connect(af_low_,  QOverload<int>::of(&QSpinBox::valueChanged), this, apply_band);
    connect(af_high_, QOverload<int>::of(&QSpinBox::valueChanged), this, apply_band);
    connect(af_duty_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        try { af->set_duty_cycle(static_cast<float>(v)); }
        catch (const std::exception& e) { emit error_message(QString::fromUtf8(e.what())); }
    });
    connect(af_start_thr_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        try { af->set_start_threshold(static_cast<uint32_t>(v)); }
        catch (const std::exception& e) { emit error_message(QString::fromUtf8(e.what())); }
    });
    connect(af_stop_thr_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!controller_) return;
        auto* af = controller_->anti_flicker_facility();
        if (!af) return;
        try { af->set_stop_threshold(static_cast<uint32_t>(v)); }
        catch (const std::exception& e) { emit error_message(QString::fromUtf8(e.what())); }
    });

    // Trail Filter
    connect(tf_enable_, &QCheckBox::toggled, this, [this](bool on) {
        if (!controller_) return;
        auto* tf = controller_->trail_filter_facility();
        if (!tf) return;
        try { tf->enable(on); } catch (const std::exception& e) {
            emit error_message(QString::fromUtf8(e.what()));
            QSignalBlocker b(tf_enable_); tf_enable_->setChecked(!on);
        }
    });
    connect(tf_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!controller_) return;
        auto* tf = controller_->trail_filter_facility();
        if (!tf) return;
        const auto t = static_cast<Metavision::I_EventTrailFilterModule::Type>(
            tf_type_->currentData().toInt());
        try { tf->set_type(t); } catch (const std::exception& e) {
            emit error_message(QString::fromUtf8(e.what()));
        }
    });
    connect(tf_threshold_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!controller_) return;
        auto* tf = controller_->trail_filter_facility();
        if (!tf) return;
        try { tf->set_threshold(static_cast<uint32_t>(v)); }
        catch (const std::exception& e) { emit error_message(QString::fromUtf8(e.what())); }
    });

    // ERC
    connect(erc_enable_, &QCheckBox::toggled, this, [this](bool on) {
        if (!controller_) return;
        auto* erc = controller_->erc_facility();
        if (!erc) return;
        try { erc->enable(on); } catch (const std::exception& e) {
            emit error_message(QString::fromUtf8(e.what()));
            QSignalBlocker b(erc_enable_); erc_enable_->setChecked(!on);
        }
    });
    connect(erc_rate_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!controller_) return;
        auto* erc = controller_->erc_facility();
        if (!erc) return;
        try { erc->set_cd_event_rate(static_cast<uint32_t>(v)); }
        catch (const std::exception& e) { emit error_message(QString::fromUtf8(e.what())); }
    });
}

void EspPanel::on_camera_connected(CameraController* controller) {
    controller_ = controller;
    populate();
}

void EspPanel::on_camera_disconnected() {
    controller_ = nullptr;
    set_all_enabled(false);
    hint_label_->setText(tr("No live camera connected."));
    hint_label_->setStyleSheet("color: #888; font-style: italic;");
}

void EspPanel::populate() {
    if (!controller_) return;
    populate_antiflicker();
    populate_trail();
    populate_erc();

    const bool any = controller_->anti_flicker_facility() ||
                     controller_->trail_filter_facility() ||
                     controller_->erc_facility();
    if (any) {
        hint_label_->setText(tr("ESP facilities loaded. Edits apply immediately."));
        hint_label_->setStyleSheet("color: #444;");
    } else {
        hint_label_->setText(tr("No ESP facilities available on this camera."));
        hint_label_->setStyleSheet("color: #888; font-style: italic;");
    }
    set_all_enabled(true);
}

void EspPanel::populate_antiflicker() {
    auto* af = controller_ ? controller_->anti_flicker_facility() : nullptr;
    if (!af) { af_group_->setEnabled(false); return; }
    af_group_->setEnabled(true);
    try {
        QSignalBlocker b0(af_enable_); af_enable_->setChecked(af->is_enabled());
        QSignalBlocker b1(af_mode_);
        const auto m = af->get_filtering_mode();
        af_mode_->setCurrentIndex(m == Metavision::I_AntiFlickerModule::BAND_PASS ? 1 : 0);
        QSignalBlocker b2(af_low_);
        QSignalBlocker b3(af_high_);
        af_low_->setValue(static_cast<int>(af->get_band_low_frequency()));
        af_high_->setValue(static_cast<int>(af->get_band_high_frequency()));
        const uint32_t min_f = af->get_min_supported_frequency();
        const uint32_t max_f = af->get_max_supported_frequency();
        af_low_->setRange(static_cast<int>(min_f), static_cast<int>(max_f));
        af_high_->setRange(static_cast<int>(min_f), static_cast<int>(max_f));
        QSignalBlocker b4(af_duty_);
        af_duty_->setRange(af->get_min_supported_duty_cycle(),
                           af->get_max_supported_duty_cycle());
        af_duty_->setValue(af->get_duty_cycle());
        QSignalBlocker b5(af_start_thr_);
        af_start_thr_->setRange(static_cast<int>(af->get_min_supported_start_threshold()),
                                static_cast<int>(af->get_max_supported_start_threshold()));
        af_start_thr_->setValue(static_cast<int>(af->get_start_threshold()));
        QSignalBlocker b6(af_stop_thr_);
        af_stop_thr_->setRange(static_cast<int>(af->get_min_supported_stop_threshold()),
                               static_cast<int>(af->get_max_supported_stop_threshold()));
        af_stop_thr_->setValue(static_cast<int>(af->get_stop_threshold()));
    } catch (const std::exception& e) {
        emit error_message(tr("Anti-Flicker init: %1").arg(QString::fromUtf8(e.what())));
    }
}

void EspPanel::populate_trail() {
    auto* tf = controller_ ? controller_->trail_filter_facility() : nullptr;
    if (!tf) { tf_group_->setEnabled(false); return; }
    tf_group_->setEnabled(true);
    try {
        std::set<Metavision::I_EventTrailFilterModule::Type> avail;
        try { avail = tf->get_available_types(); } catch (...) {}
        // Filter the combo to only supported types.
        for (int i = tf_type_->count() - 1; i >= 0; --i) {
            const auto t = static_cast<Metavision::I_EventTrailFilterModule::Type>(
                tf_type_->itemData(i).toInt());
            if (!avail.empty() && avail.find(t) == avail.end()) {
                tf_type_->removeItem(i);
            }
        }
        QSignalBlocker b0(tf_enable_); tf_enable_->setChecked(tf->is_enabled());
        QSignalBlocker b1(tf_type_);
        const auto cur = tf->get_type();
        const int idx = tf_type_->findData(static_cast<int>(cur));
        if (idx >= 0) tf_type_->setCurrentIndex(idx);
        QSignalBlocker b2(tf_threshold_);
        tf_threshold_->setRange(static_cast<int>(tf->get_min_supported_threshold()),
                                static_cast<int>(tf->get_max_supported_threshold()));
        tf_threshold_->setValue(static_cast<int>(tf->get_threshold()));
    } catch (const std::exception& e) {
        emit error_message(tr("Trail Filter init: %1").arg(QString::fromUtf8(e.what())));
    }
}

void EspPanel::populate_erc() {
    auto* erc = controller_ ? controller_->erc_facility() : nullptr;
    if (!erc) { erc_group_->setEnabled(false); return; }
    erc_group_->setEnabled(true);
    try {
        QSignalBlocker b0(erc_enable_); erc_enable_->setChecked(erc->is_enabled());
        QSignalBlocker b1(erc_rate_);
        erc_rate_->setRange(static_cast<int>(erc->get_min_supported_cd_event_rate()),
                            static_cast<int>(erc->get_max_supported_cd_event_rate()));
        erc_rate_->setValue(static_cast<int>(erc->get_cd_event_rate()));
    } catch (const std::exception& e) {
        emit error_message(tr("ERC init: %1").arg(QString::fromUtf8(e.what())));
    }
}

void EspPanel::set_all_enabled(bool on) {
    // Group boxes are individually disabled by populate_* when the facility
    // is missing; here we only flip the top-level enable for the case where
    // no camera is connected at all.
    if (!on) {
        af_group_->setEnabled(false);
        tf_group_->setEnabled(false);
        erc_group_->setEnabled(false);
    }
}

} // namespace gui
