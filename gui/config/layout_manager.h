// gui/config/layout_manager.h — main-window layout save/restore.
//
// Phase 10 (design §5.6): persists the QMainWindow geometry + dock widget
// layout to a JSON file so the user's customized arrangement is restored on
// the next launch. Also persists the active panel visibility toggles.

#ifndef GUI_CONFIG_LAYOUT_MANAGER_H
#define GUI_CONFIG_LAYOUT_MANAGER_H

#include <QObject>
#include <QString>

class QMainWindow;

namespace gui {

class LayoutManager : public QObject {
    Q_OBJECT
public:
    explicit LayoutManager(QMainWindow* main_window, QObject* parent = nullptr);

    /// @brief Saves the current layout to @p path.
    bool save(const QString& path) const;

    /// @brief Restores the layout from @p path.
    bool load(const QString& path);

    /// @brief Saves to the default location (~/.config/GUI-for-openEB/layout.json).
    bool save_default() const;

    /// @brief Loads from the default location.
    bool load_default();

    /// @brief Resets the main window to its default layout (clears docks state).
    void reset_layout();

    /// @brief Captures the current geometry + dock state as the "factory"
    /// default that reset_layout() restores. Call once after the main window
    /// is fully built (all docks added) but before load_default() so the
    /// default reflects the intended initial arrangement, not a user-customized one.
    void capture_default();

private:
    QString default_path() const;
    QMainWindow* main_window_{nullptr};
    QByteArray default_geometry_;
    QByteArray default_state_;
};

} // namespace gui

#endif // GUI_CONFIG_LAYOUT_MANAGER_H
