// gui/panels/display_panel.h — frame display parameters (accumulation, palette).
//
// Phase 1 scope (design §3.2.1): accumulation time + color theme.
// Frame-mode selection (7 modes) is wired in Phase 5; the combo is present
// but disabled here.

#ifndef GUI_PANELS_DISPLAY_PANEL_H
#define GUI_PANELS_DISPLAY_PANEL_H

#include <QWidget>

class QSlider;
class QDoubleSpinBox;
class QComboBox;

namespace gui {

class DisplayPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisplayPanel(QWidget* parent = nullptr);

    int accumulation_time_us() const;
    int color_palette_index() const;
    int frame_mode_index() const;

    /// @brief Sets the active frame mode (0..6). See design §3.2.2.
    void set_frame_mode(int index);

    /// @brief Sets the accumulation time (ms) and updates the slider/spin
    /// without emitting signals. Used to sync the UI when a frame-mode preset
    /// is selected so the two controls don't show stale values.
    void set_accumulation_time_ms(double ms);

signals:
    void accumulation_time_changed_us(int us);
    void color_palette_changed(int index); // 0=Dark,1=Light,2=CoolWarm,3=Gray
    void frame_mode_changed(int index);    // 0=Diff,1=Integration,2=Histogram,
                                           // 3=TimeDecay,4=ContrastMap,5=Periodic,
                                           // 6=OnDemand

private:
    QSlider* accum_slider_{nullptr};
    QDoubleSpinBox* accum_spin_{nullptr};
    QComboBox* palette_combo_{nullptr};
    QComboBox* mode_combo_{nullptr};
};

} // namespace gui

#endif // GUI_PANELS_DISPLAY_PANEL_H
