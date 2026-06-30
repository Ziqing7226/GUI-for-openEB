// gui/config/layout_manager.cpp

#include "layout_manager.h"

#include <QByteArray>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QStandardPaths>

namespace gui {

LayoutManager::LayoutManager(QMainWindow* main_window, QObject* parent)
    : QObject(parent), main_window_(main_window) {}

QString LayoutManager::default_path() const {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) return QStringLiteral("layout.json");
    QDir().mkpath(base);
    return base + QStringLiteral("/layout.json");
}

bool LayoutManager::save(const QString& path) const {
    if (!main_window_) return false;
    QJsonObject obj;
    obj["geometry"] = QString::fromLatin1(
        main_window_->saveGeometry().toBase64());
    obj["state"] = QString::fromLatin1(
        main_window_->saveState().toBase64());
    QJsonDocument doc(obj);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    if (f.write(data) != data.size() || !f.flush()) return false;
    return true;
}

bool LayoutManager::load(const QString& path) {
    if (!main_window_) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const auto obj = doc.object();
    if (obj.contains("geometry")) {
        const QByteArray g = QByteArray::fromBase64(
            obj.value("geometry").toString().toLatin1());
        main_window_->restoreGeometry(g);
    }
    if (obj.contains("state")) {
        const QByteArray s = QByteArray::fromBase64(
            obj.value("state").toString().toLatin1());
        main_window_->restoreState(s);
    }
    return true;
}

bool LayoutManager::save_default() const {
    return save(default_path());
}

bool LayoutManager::load_default() {
    return load(default_path());
}

void LayoutManager::capture_default() {
    if (!main_window_) return;
    default_geometry_ = main_window_->saveGeometry();
    default_state_ = main_window_->saveState();
}

void LayoutManager::reset_layout() {
    if (!main_window_) return;
    // restoreState() / restoreGeometry() silently do nothing when passed an
    // empty QByteArray, so the old code that called restoreState(QByteArray())
    // was a no-op. We restore from the snapshot captured by capture_default()
    // (taken at startup after all docks were added). If no snapshot exists
    // (capture_default never called), we fall back to showing all docks.
    if (!default_geometry_.isEmpty()) {
        main_window_->restoreGeometry(default_geometry_);
    }
    if (!default_state_.isEmpty()) {
        main_window_->restoreState(default_state_);
    } else {
        // No captured default — at least make all docks visible.
        const auto docks = main_window_->findChildren<QDockWidget*>();
        for (auto* d : docks) d->show();
    }
}

} // namespace gui
