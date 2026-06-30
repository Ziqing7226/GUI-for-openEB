// gui/widgets/multi_window_manager.h — multi-window parallel display layout.
//
// Phase 10 (design §5.6): spawns additional dock/MDI windows for parallel
// display of different algorithm outputs. New windows are auto-tiled; the
// user can drag them anywhere. Built on QMdiArea so the windows can be
// cascaded, tiled, tabbed or floated.

#ifndef GUI_WIDGETS_MULTI_WINDOW_MANAGER_H
#define GUI_WIDGETS_MULTI_WINDOW_MANAGER_H

#include <QMainWindow>
#include <QMdiArea>
#include <memory>
#include <vector>

class QMdiSubWindow;

namespace gui {

class EventDisplayWidget;

/// @brief Manages additional display windows for multi-window layout.
class MultiWindowManager : public QObject {
    Q_OBJECT
public:
    explicit MultiWindowManager(QMainWindow* parent);
    ~MultiWindowManager();

    /// @brief Creates a new MDI sub-window containing a fresh event display.
    /// @param title Window title.
    /// @return The created EventDisplayWidget, or nullptr on failure.
    EventDisplayWidget* add_display(const QString& title);

    /// @brief Tiles all sub-windows in a grid.
    void tile();

    /// @brief Cascades all sub-windows.
    void cascade();

    /// @brief Closes all sub-windows.
    void close_all();

    /// @brief Returns the QMdiArea used for sub-windows. The caller may add
    /// the area as a dock or central widget.
    QMdiArea* mdi_area() { return mdi_area_; }

    std::size_t window_count() const { return sub_windows_.size(); }

signals:
    void window_added(EventDisplayWidget* w);
    void window_removed(EventDisplayWidget* w);

private:
    QMainWindow* main_window_{nullptr};
    QMainWindow* host_window_{nullptr};  ///< Top-level window hosting the MDI area.
    QMdiArea* mdi_area_{nullptr};
    std::vector<QMdiSubWindow*> sub_windows_;
};

} // namespace gui

#endif // GUI_WIDGETS_MULTI_WINDOW_MANAGER_H
