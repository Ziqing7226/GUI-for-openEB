// gui/recorder/playback_controls.cpp

#include "playback_controls.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>

#include "playback_controller.h"

namespace gui {

PlaybackControls::PlaybackControls(QWidget* parent) : QWidget(parent) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 2, 6, 2);

    btn_play_ = new QPushButton(tr("Play"), this);
    btn_step_ = new QPushButton(tr("Step"), this);
    slider_   = new QSlider(Qt::Horizontal, this);
    lbl_cur_  = new QLabel("0.000 s", this);
    lbl_dur_  = new QLabel("0.000 s", this);
    cmb_speed_ = new QComboBox(this);
    chk_loop_ = new QCheckBox(tr("Loop"), this);

    cmb_speed_->addItems({"0.25x", "0.5x", "1x", "2x", "4x", tr("Max")});
    cmb_speed_->setCurrentIndex(2);

    slider_->setMinimum(0);
    slider_->setMaximum(1000);

    lay->addWidget(btn_play_);
    lay->addWidget(btn_step_);
    lay->addWidget(lbl_cur_, 0);
    lay->addWidget(slider_, 1);
    lay->addWidget(lbl_dur_, 0);
    lay->addWidget(cmb_speed_);
    lay->addWidget(chk_loop_);

    activate(false);

    connect(btn_play_, &QPushButton::clicked, this, [this]() {
        if (controller_) controller_->toggle_play_pause();
    });
    connect(btn_step_, &QPushButton::clicked, this, [this]() {
        if (!controller_) return;
        // Pause first so we step from a known position, then seek forward by
        // one frame interval (~33 ms at 30 fps). Finally, briefly start the
        // camera so the SDK decodes events at the new position and the frame
        // pipeline emits a fresh frame; pause again shortly after so the user
        // sees exactly one step's worth of motion. Without the start/pause,
        // seeking while paused produces no new frame (the decoder is idle and
        // CDFrameGenerator has no events to accumulate).
        controller_->pause();
        const Metavision::timestamp next = controller_->position_us() + 33000;
        if (controller_->seek(next)) {
            controller_->play();
            QTimer::singleShot(120, this, [this]() { controller_->pause(); });
        }
    });
    // Tie seeking_ to the press/release lifecycle so the 100 ms probe timer
    // cannot yank the slider knob out from under the cursor mid-drag.
    connect(slider_, &QSlider::sliderPressed,  this, [this]() { seeking_ = true;  });
    connect(slider_, &QSlider::sliderReleased, this, [this]() { seeking_ = false; });
    connect(slider_, &QSlider::sliderMoved,    this, &PlaybackControls::on_slider_moved);
    connect(cmb_speed_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!controller_) return;
        double speeds[] = {0.25, 0.5, 1.0, 2.0, 4.0, 0.0};
        controller_->set_speed(speeds[idx]);
    });
    connect(chk_loop_, &QCheckBox::toggled, this, [this](bool on) {
        if (controller_) controller_->set_loop(on);
    });
}

void PlaybackControls::set_controller(PlaybackController* controller) {
    // Disconnect the previous controller's signals to avoid duplicate
    // handlers when set_controller is called more than once. Early-return
    // when unchanged so a repeat call does not create duplicate connections
    // (Qt's default AutoConnection allows duplicates).
    if (controller_ == controller) return;
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &PlaybackController::state_changed,
                this, &PlaybackControls::on_state_changed);
        connect(controller_, &PlaybackController::position_changed,
                this, &PlaybackControls::on_position_changed);
        connect(controller_, &PlaybackController::opened,
                this, &PlaybackControls::on_opened);
        connect(controller_, &PlaybackController::speed_changed,
                this, &PlaybackControls::on_speed_changed);
        connect(controller_, &PlaybackController::loop_changed,
                this, &PlaybackControls::on_loop_changed);
    }
}

void PlaybackControls::activate(bool on) {
    setVisible(on);
    setEnabled(on);
}

void PlaybackControls::on_state_changed(bool playing) {
    btn_play_->setText(playing ? tr("Pause") : tr("Play"));
}

void PlaybackControls::on_opened(Metavision::timestamp dur) {
    lbl_dur_->setText(format_time(dur));
    slider_->setValue(0);
}

void PlaybackControls::on_position_changed(Metavision::timestamp pos, Metavision::timestamp dur) {
    // Always update the time label — even mid-drag the user wants to see the
    // timestamp they are scrubbing to (seek() emits position_changed with the
    // target time). Only the slider knob is left alone while dragging so the
    // 100 ms probe timer cannot yank it out from under the cursor.
    lbl_cur_->setText(format_time(pos));
    if (seeking_) return;
    if (dur > 0) {
        slider_->blockSignals(true);
        slider_->setValue(static_cast<int>(pos * slider_->maximum() / dur));
        slider_->blockSignals(false);
    }
}

void PlaybackControls::on_slider_moved(int v) {
    if (!controller_) return;
    // seeking_ is now managed by sliderPressed/sliderReleased so the probe
    // timer cannot fight the drag; no need to toggle it here.
    const Metavision::timestamp dur = controller_->duration_us();
    if (dur > 0) {
        controller_->seek(v * dur / slider_->maximum());
    }
}

void PlaybackControls::on_speed_changed(double s) {
    // Sync the combo to the actually-applied speed. set_speed() may roll back
    // to the previous speed on failure (e.g. reopen failed), and the speed can
    // also change programmatically (e.g. on file open). Without this the combo
    // would display a speed that doesn't match reality.
    int target = 2; // default 1x
    if (s == 0.0)       target = 5; // Max
    else if (s == 0.25) target = 0;
    else if (s == 0.5)  target = 1;
    else if (s == 1.0)  target = 2;
    else if (s == 2.0)  target = 3;
    else if (s == 4.0)  target = 4;
    QSignalBlocker b(cmb_speed_);
    cmb_speed_->setCurrentIndex(target);
}

void PlaybackControls::on_loop_changed(bool on) {
    chk_loop_->blockSignals(true);
    chk_loop_->setChecked(on);
    chk_loop_->blockSignals(false);
}

QString PlaybackControls::format_time(Metavision::timestamp us) {
    return QStringLiteral("%1 s").arg(us / 1.0e6, 0, 'f', 3);
}

} // namespace gui
