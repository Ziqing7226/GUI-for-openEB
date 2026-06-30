// gui/panels/devices_panel.h — live camera discovery + connect controls.

#ifndef GUI_PANELS_DEVICES_PANEL_H
#define GUI_PANELS_DEVICES_PANEL_H

#include <QWidget>
#include <QString>

class QListWidget;
class QPushButton;

namespace gui {

class DevicesPanel : public QWidget {
    Q_OBJECT
public:
    explicit DevicesPanel(QWidget* parent = nullptr);

public slots:
    void refresh_sources(const std::vector<std::pair<QString, QString>>& sources);
    void set_connected(bool connected);

signals:
    void refresh_requested();
    void connect_first_requested();
    void connect_serial_requested(const QString& serial);
    void disconnect_requested();

private:
    QListWidget* list_{nullptr};
    QPushButton* btn_refresh_{nullptr};
    QPushButton* btn_connect_first_{nullptr};
    QPushButton* btn_connect_selected_{nullptr};
    QPushButton* btn_disconnect_{nullptr};
};

} // namespace gui

#endif // GUI_PANELS_DEVICES_PANEL_H
