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

signals:
    void accumulation_time_changed_us(int us);
    void color_palette_changed(int index); // 0=Dark,1=Light,2=CoolWarm,3=Gray

private:
    QSlider* accum_slider_{nullptr};
    QDoubleSpinBox* accum_spin_{nullptr};
    QComboBox* palette_combo_{nullptr};
    QComboBox* mode_combo_{nullptr};
};

} // namespace gui

#endif // GUI_PANELS_DISPLAY_PANEL_H
