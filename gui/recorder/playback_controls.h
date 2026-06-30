// gui/recorder/playback_controls.h — bottom-bar playback transport (design §3.3.2).
//
// Shown only while a file is open. Provides play/pause, seek slider, time
// labels, speed selector and a loop toggle. Delegates all logic to the
// PlaybackController.

#ifndef GUI_RECORDER_PLAYBACK_CONTROLS_H
#define GUI_RECORDER_PLAYBACK_CONTROLS_H

#include <QWidget>
#include <QModelIndex>

#include <metavision/sdk/base/utils/timestamp.h>

class QSlider;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;

namespace gui {

class PlaybackController;

class PlaybackControls : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackControls(QWidget* parent = nullptr);

    void set_controller(PlaybackController* controller);
    /// @brief Show/hide the bar (only relevant for file playback).
    void activate(bool on);

signals:
    /// @brief Emitted when the user drags a seek range to crop (design §3.3.3).
    void crop_range_requested(Metavision::timestamp start_us, Metavision::timestamp end_us);

private slots:
    void on_state_changed(bool playing);
    void on_position_changed(Metavision::timestamp pos, Metavision::timestamp dur);
    void on_opened(Metavision::timestamp dur);
    void on_slider_moved(int v);
    void on_speed_changed(double s);
    void on_loop_changed(bool on);

private:
    static QString format_time(Metavision::timestamp us);

    PlaybackController* controller_{nullptr};
    QPushButton* btn_play_{nullptr};
    QPushButton* btn_step_{nullptr};
    QSlider* slider_{nullptr};
    QLabel* lbl_cur_{nullptr};
    QLabel* lbl_dur_{nullptr};
    QComboBox* cmb_speed_{nullptr};
    QCheckBox* chk_loop_{nullptr};
    bool seeking_{false};
};

} // namespace gui

#endif // GUI_RECORDER_PLAYBACK_CONTROLS_H
