// gui/panels/devices_panel.cpp

#include "devices_panel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace gui {

DevicesPanel::DevicesPanel(QWidget* parent) : AbstractPanel(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list_);

    auto* row1 = new QHBoxLayout();
    btn_refresh_ = new QPushButton(tr("Refresh"), this);
    btn_connect_first_ = new QPushButton(tr("Connect first"), this);
    row1->addWidget(btn_refresh_);
    row1->addWidget(btn_connect_first_);
    layout->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    btn_connect_selected_ = new QPushButton(tr("Connect selected"), this);
    btn_disconnect_ = new QPushButton(tr("Disconnect"), this);
    row2->addWidget(btn_connect_selected_);
    row2->addWidget(btn_disconnect_);
    layout->addLayout(row2);

    set_connected(false);

    connect(btn_refresh_, &QPushButton::clicked, this, &DevicesPanel::refresh_requested);
    connect(btn_connect_first_, &QPushButton::clicked, this, &DevicesPanel::connect_first_requested);
    connect(btn_connect_selected_, &QPushButton::clicked, this, [this]() {
        auto* item = list_->currentItem();
        if (!item) {
            return;
        }
        const QString serial = item->data(Qt::UserRole).toString();
        if (!serial.isEmpty()) {
            emit connect_serial_requested(serial);
        }
    });
    connect(btn_disconnect_, &QPushButton::clicked, this, &DevicesPanel::disconnect_requested);
}

void DevicesPanel::refresh_sources(const std::vector<std::pair<QString, QString>>& sources) {
    list_->clear();
    for (const auto& src : sources) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(src.first, src.second), list_);
        item->setData(Qt::UserRole, src.second);
    }
    if (list_->count() == 0) {
        new QListWidgetItem(tr("No cameras detected"), list_);
    }
}

void DevicesPanel::set_connected(bool connected) {
    btn_connect_first_->setEnabled(!connected);
    btn_connect_selected_->setEnabled(!connected);
    btn_disconnect_->setEnabled(connected);
}

} // namespace gui
