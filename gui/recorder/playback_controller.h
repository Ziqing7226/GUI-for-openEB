// gui/recorder/playback_controller.h — file playback control (design §3.3.2).
//
// Wraps OfflineStreamingControl for seek / duration queries and coordinates
// play / pause / loop with the CameraController. Playback speed is applied by
// reopening the file with the appropriate FileConfigHints (real-time vs.
// as-fast-as-possible). A position probe timer polls the last event timestamp
// to drive the progress bar.

#ifndef GUI_RECORDER_PLAYBACK_CONTROLLER_H
#define GUI_RECORDER_PLAYBACK_CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <optional>

#include <metavision/sdk/base/utils/timestamp.h>

namespace gui {

class CameraController;

class PlaybackController : public QObject {
    Q_OBJECT
public:
    explicit PlaybackController(QObject* parent = nullptr);

    /// @brief Binds to a camera controller (required before any playback op).
    void set_camera(CameraController* controller);

    /// @brief Reopens the current file at @p path with the given speed.
    /// Speed 0 = as-fast-as-possible (max), otherwise a real-time multiplier.
    bool open_file(const QString& path, double speed);
    void play();
    void pause();
    void toggle_play_pause();
    bool seek(Metavision::timestamp t_us);
    void set_loop(bool on);
    void set_speed(double speed);

    bool is_playing() const { return playing_; }
    bool loop() const { return loop_; }
    double speed() const { return speed_; }
    Metavision::timestamp duration_us() const;
    Metavision::timestamp position_us() const;

    bool available() const;

signals:
    void opened(Metavision::timestamp duration_us);
    void closed();
    void state_changed(bool playing);
    void position_changed(Metavision::timestamp pos_us, Metavision::timestamp dur_us);
    void speed_changed(double speed);
    void loop_changed(bool on);
    void error(const QString& msg);

private:
    void probe_position();
    Metavision::timestamp query_duration() const;
    Metavision::timestamp query_position() const;

    QPointer<CameraController> controller_;
    QString path_;
    double speed_{1.0};
    bool loop_{false};
    bool playing_{false};
    QTimer probe_timer_;
    Metavision::timestamp duration_us_{0};
};

} // namespace gui

#endif // GUI_RECORDER_PLAYBACK_CONTROLLER_H
