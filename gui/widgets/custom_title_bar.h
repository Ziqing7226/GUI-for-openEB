// gui/widgets/custom_title_bar.h — custom title bar for the frameless main window.
//
// Inspired by VSCode's titlebar part (src/vs/workbench/browser/parts/titlebar/
// titlebarPart.ts): the native WM title bar is removed (Qt::FramelessWindowHint)
// and a custom widget is drawn in its place. The background color is set
// directly via setColors(), so it always follows the application theme —
// unlike the native title bar whose color is controlled by the window manager
// and cannot be changed reliably from Qt.
//
// Layout (left to right):
//   [menu bar] [stretch] [title label] [stretch] [minimize] [maximize] [close]
//
// Dragging the title bar moves the window (QWindow::startSystemMove).
// Double-clicking toggles maximize/restore.
//
// ResizeGrip provides 8 edge/corner resize handles so the frameless window
// remains resizable (QWindow::startSystemResize).

#ifndef GUI_WIDGETS_CUSTOM_TITLE_BAR_H
#define GUI_WIDGETS_CUSTOM_TITLE_BAR_H

#include <QColor>
#include <QWidget>

class QLabel;
class QMenuBar;
class QPushButton;

namespace gui {

/// @brief Custom title bar widget — replaces the native WM title bar.
///
/// The background color is set directly (not via the WM), so it always
/// matches the application theme. See the VSCode reference above.
class CustomTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);

    /// Reparents @p menu_bar into the title bar (left side).
    void setMenuBar(QMenuBar* menu_bar);

    /// Sets the window title text shown in the center.
    void setTitle(const QString& title);

    /// Sets the background and text colors.
    /// @p bg should be slightly darker than the main window background.
    void setColors(const QColor& bg, const QColor& fg);

    QSize sizeHint() const override { return {0, 32}; }
    QSize minimumSizeHint() const override { return {0, 32}; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QMenuBar* menu_bar_{nullptr};
    QLabel* title_label_{nullptr};
    QPushButton* btn_min_{nullptr};
    QPushButton* btn_max_{nullptr};
    QPushButton* btn_close_{nullptr};
    QColor bg_color_;
    QColor fg_color_;
};

/// @brief Invisible resize handle on a window edge or corner.
///
/// Positioned by the parent window's resizeEvent. On mouse press, calls
/// QWindow::startSystemResize() which lets the WM handle the resize gesture.
class ResizeGrip : public QWidget {
    Q_OBJECT
public:
    enum class Position {
        Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };
    explicit ResizeGrip(Position pos, QWidget* parent = nullptr);

    /// Repositions the grip within the parent widget's rect.
    void reposition(const QRect& parent_rect);

    static constexpr int kEdgeThickness = 5;
    static constexpr int kCornerSize = 12;

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    Position pos_;
};

} // namespace gui

#endif // GUI_WIDGETS_CUSTOM_TITLE_BAR_H
