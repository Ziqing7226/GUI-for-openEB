// gui/panels/bias_tweaks_panel.cpp

#include "bias_tweaks_panel.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>

#include <cstddef>
#include <map>

#include <metavision/hal/facilities/i_ll_biases.h>

#include "app/camera_controller.h"

namespace gui {

namespace {
// Candidate bias names per sensor family; the first that exists wins.
const char* kDiffOnNames[]  = {"bias_diff_on", "DiffOn", "diff_on"};
const char* kDiffOffNames[] = {"bias_diff_off", "DiffOff", "diff_off"};
const char* kPrNames[]      = {"bias_pr", "Pr", "pr"};
const char* kRefrNames[]    = {"bias_refr", "Refr", "refr"};

// Threshold slider reference point (slider value at which multiplier == 1.0).
constexpr int kThresholdRef = 15;
} // namespace

BiasTweaksPanel::BiasTweaksPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    hint_label_ = new QLabel(tr("Advanced DVS bias tweaks (threshold / bandwidth / "
                                "max firing rate / ON-OFF balance)."), this);
    hint_label_->setWordWrap(true);
    hint_label_->setStyleSheet("color: #888; font-style: italic;");
    outer->addWidget(hint_label_);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);

    auto add_tweak = [&](int row, const QString& title, QSlider*& slider,
                         QLabel*& readout, int lo, int hi, int def) {
        auto* name = new QLabel(title, this);
        grid->addWidget(name, row, 0);
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(lo, hi);
        slider->setValue(def);
        grid->addWidget(slider, row, 1);
        readout = new QLabel(this);
        readout->setMinimumWidth(160);
        readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(readout, row, 2);
    };

    add_tweak(0, tr("Threshold"),     threshold_slider_, threshold_readout_,
              1, 50, kThresholdRef);
    add_tweak(1, tr("Bandwidth"),     bandwidth_slider_, bandwidth_readout_,
              1, 2000, 100);
    add_tweak(2, tr("Max firing rate"), max_rate_slider_, max_rate_readout_,
              10, 3000, 1000);
    add_tweak(3, tr("ON-OFF balance"), balance_slider_, balance_readout_,
              -100, 100, 0);
    outer->addLayout(grid);

    auto* btn_row = new QVBoxLayout();
    reset_btn_ = new QPushButton(tr("Reset to snapshot"), this);
    btn_row->addWidget(reset_btn_);
    outer->addLayout(btn_row);
    outer->addStretch(1);

    connect(threshold_slider_, &QSlider::valueChanged,
            this, &BiasTweaksPanel::on_threshold_changed);
    connect(bandwidth_slider_, &QSlider::valueChanged,
            this, &BiasTweaksPanel::on_bandwidth_changed);
    connect(max_rate_slider_, &QSlider::valueChanged,
            this, &BiasTweaksPanel::on_max_rate_changed);
    connect(balance_slider_, &QSlider::valueChanged,
            this, &BiasTweaksPanel::on_balance_changed);
    connect(reset_btn_, &QPushButton::clicked, this, &BiasTweaksPanel::on_reset);

    enable_controls(false);
    refresh_readouts();
}

void BiasTweaksPanel::on_camera_connected(CameraController* controller) {
    controller_ = controller;
    populate_from_camera();
}

void BiasTweaksPanel::on_camera_disconnected() {
    controller_ = nullptr;
    snap_ = Snapshot{};
    enable_controls(false);
    hint_label_->setText(tr("No live camera connected."));
    hint_label_->setStyleSheet("color: #888; font-style: italic;");
}

void BiasTweaksPanel::enable_controls(bool on) {
    threshold_slider_->setEnabled(on);
    bandwidth_slider_->setEnabled(on);
    max_rate_slider_->setEnabled(on);
    balance_slider_->setEnabled(on);
    reset_btn_->setEnabled(on);
}

void BiasTweaksPanel::set_bias(const std::string& name, int value) {
    if (!controller_ || name.empty()) return;
    auto* b = controller_->biases_facility();
    if (!b) return;
    try {
        b->set(name, value);
    } catch (const std::exception& e) {
        emit error_message(QString::fromUtf8(e.what()));
    }
}

int BiasTweaksPanel::threshold_to_diff(int slider_v) const {
    // Base diff = mean of snapshot on/off; slider scales it (ref point = 1.0×).
    const double base = (snap_.has_diff_on && snap_.has_diff_off)
                            ? (snap_.diff_on + snap_.diff_off) * 0.5
                            : 130.0;
    const double mult = static_cast<double>(slider_v) / kThresholdRef;
    int v = static_cast<int>(base * mult);
    return (v > 0) ? v : 1;
}

int BiasTweaksPanel::bandwidth_to_pr(int hz) const {
    const double base = snap_.has_pr ? snap_.pr : 200.0;
    const double mult = static_cast<double>(hz) / 100.0;
    int v = static_cast<int>(base * mult);
    return (v > 0) ? v : 1;
}

int BiasTweaksPanel::max_rate_to_refr(int hz) const {
    // Refractory period ∝ 1/maxRate; scale relative to snapshot at 1000 Hz.
    const double base = snap_.has_refr ? snap_.refr : 100.0;
    const double mult = 1000.0 / static_cast<double>(hz);
    int v = static_cast<int>(base * mult);
    return (v > 0) ? v : 1;
}

void BiasTweaksPanel::apply_threshold_and_balance() {
    if (applying_) return;
    const int base_diff = threshold_to_diff(threshold_slider_->value());
    const double b = balance_slider_->value() / 100.0;  // [-1, 1]
    // Positive balance → favour ON: lower diff_on, raise diff_off.
    const double on_factor  = 1.0 - 0.5 * b;
    const double off_factor = 1.0 + 0.5 * b;
    if (snap_.has_diff_on) {
        int v = static_cast<int>(base_diff * on_factor);
        if (v < 1) v = 1;
        set_bias(snap_.name_diff_on, v);
    }
    if (snap_.has_diff_off) {
        int v = static_cast<int>(base_diff * off_factor);
        if (v < 1) v = 1;
        set_bias(snap_.name_diff_off, v);
    }
}

void BiasTweaksPanel::on_threshold_changed(int /*v*/) {
    refresh_readouts();
    apply_threshold_and_balance();
}

void BiasTweaksPanel::on_bandwidth_changed(int /*v*/) {
    refresh_readouts();
    if (snap_.has_pr) {
        set_bias(snap_.name_pr, bandwidth_to_pr(bandwidth_slider_->value()));
    }
}

void BiasTweaksPanel::on_max_rate_changed(int /*v*/) {
    refresh_readouts();
    if (snap_.has_refr) {
        set_bias(snap_.name_refr, max_rate_to_refr(max_rate_slider_->value()));
    }
}

void BiasTweaksPanel::on_balance_changed(int /*v*/) {
    refresh_readouts();
    apply_threshold_and_balance();
}

void BiasTweaksPanel::on_reset() {
    if (!controller_) return;
    QSignalBlocker b1(threshold_slider_);
    QSignalBlocker b2(bandwidth_slider_);
    QSignalBlocker b3(max_rate_slider_);
    QSignalBlocker b4(balance_slider_);
    threshold_slider_->setValue(kThresholdRef);
    bandwidth_slider_->setValue(100);
    max_rate_slider_->setValue(1000);
    balance_slider_->setValue(0);
    // Restore snapshot biases directly.
    if (snap_.has_diff_on)  set_bias(snap_.name_diff_on, snap_.diff_on);
    if (snap_.has_diff_off) set_bias(snap_.name_diff_off, snap_.diff_off);
    if (snap_.has_pr)       set_bias(snap_.name_pr, snap_.pr);
    if (snap_.has_refr)     set_bias(snap_.name_refr, snap_.refr);
    refresh_readouts();
    emit info_message(tr("Bias tweaks reset to snapshot."));
}

void BiasTweaksPanel::refresh_readouts() {
    // Threshold → contrast estimate (DVS fires on ~ln(1+c) intensity change).
    const int thr = threshold_slider_->value();
    threshold_readout_->setText(tr("≈ %1% contrast").arg(thr));

    const int bw = bandwidth_slider_->value();
    bandwidth_readout_->setText(tr("≈ %1 Hz").arg(bw));

    const int mr = max_rate_slider_->value();
    const double refr_us = (mr > 0) ? 1.0e6 / mr : 0.0;
    max_rate_readout_->setText(tr("≈ %1 Hz  (refr %2 µs)")
                                   .arg(mr)
                                   .arg(refr_us, 0, 'f', 0));

    const double bal = balance_slider_->value() / 100.0;
    const double on_factor  = 1.0 - 0.5 * bal;
    const double off_factor = 1.0 + 0.5 * bal;
    double ratio = (on_factor > 1e-6) ? (off_factor / on_factor) : 0.0;
    balance_readout_->setText(tr("ON:OFF ≈ %1:1").arg(ratio, 0, 'f', 2));
}

void BiasTweaksPanel::populate_from_camera() {
    if (!controller_) return;
    auto* biases = controller_->biases_facility();
    if (!biases) {
        hint_label_->setText(tr("Bias facility unavailable on this camera."));
        enable_controls(false);
        return;
    }

    snap_ = Snapshot{};
    std::map<std::string, int> all;
    try {
        all = biases->get_all_biases();
    } catch (const std::exception& e) {
        hint_label_->setText(tr("Failed to enumerate biases: %1")
                                 .arg(QString::fromUtf8(e.what())));
        enable_controls(false);
        return;
    }

    auto resolve = [&](const char* const* names, std::size_t count,
                       bool& has, int& val, std::string& resolved_name) {
        for (std::size_t i = 0; i < count; ++i) {
            auto it = all.find(names[i]);
            if (it != all.end()) {
                has = true;
                val = it->second;
                resolved_name = names[i];
                return;
            }
        }
    };
    resolve(kDiffOnNames,  sizeof(kDiffOnNames) / sizeof(kDiffOnNames[0]),
            snap_.has_diff_on, snap_.diff_on, snap_.name_diff_on);
    resolve(kDiffOffNames, sizeof(kDiffOffNames) / sizeof(kDiffOffNames[0]),
            snap_.has_diff_off, snap_.diff_off, snap_.name_diff_off);
    resolve(kPrNames,      sizeof(kPrNames) / sizeof(kPrNames[0]),
            snap_.has_pr, snap_.pr, snap_.name_pr);
    resolve(kRefrNames,    sizeof(kRefrNames) / sizeof(kRefrNames[0]),
            snap_.has_refr, snap_.refr, snap_.name_refr);

    const int found = snap_.has_diff_on + snap_.has_diff_off +
                      snap_.has_pr + snap_.has_refr;
    if (found == 0) {
        hint_label_->setText(tr("No DVS tweak biases found on this sensor."));
        hint_label_->setStyleSheet("color: #888; font-style: italic;");
        enable_controls(false);
        return;
    }

    hint_label_->setText(tr("Tweak biases available: %1/4 (diff_on/off, pr, refr).")
                             .arg(found));
    hint_label_->setStyleSheet("color: #444;");

    applying_ = true;
    QSignalBlocker b1(threshold_slider_);
    QSignalBlocker b2(bandwidth_slider_);
    QSignalBlocker b3(max_rate_slider_);
    QSignalBlocker b4(balance_slider_);
    threshold_slider_->setValue(kThresholdRef);
    bandwidth_slider_->setValue(100);
    max_rate_slider_->setValue(1000);
    balance_slider_->setValue(0);
    applying_ = false;

    enable_controls(true);
    refresh_readouts();
}

} // namespace gui
