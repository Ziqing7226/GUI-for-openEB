// gui/app/icon_provider.cpp

#include "icon_provider.h"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QPixmap>

namespace gui {

namespace {
// Cache of rendered icons keyed by "name|color". Avoids repeated file I/O
// and SVG parsing on theme refreshes (§14.2 — performance fix).
// The cache grows with the number of distinct (name, color) pairs, which is
// bounded by (icon count × distinct colors) — typically < 100 entries.
QHash<QString, QIcon>& icon_cache() {
    static QHash<QString, QIcon> cache;
    return cache;
}

QString cache_key(const QString& name, const QColor& color) {
    return name + QLatin1Char('|') + color.name();
}
} // namespace

QIcon IconProvider::get(const QString& name) {
    // The ThemeController sets the application palette's WindowText/Text to the
    // current theme's primary foreground color, so reading the palette here
    // keeps icons in sync with the active theme without a direct dependency.
    QColor fg = qApp->palette().color(QPalette::WindowText);
    if (!fg.isValid()) fg = QColor(Qt::black);
    return render(name, fg);
}

QIcon IconProvider::get(const QString& name, const QColor& color) {
    return render(name, color.isValid() ? color : QColor(Qt::black));
}

void IconProvider::clear_cache() {
    icon_cache().clear();
}

QIcon IconProvider::render(const QString& name, const QColor& color) {
    // Check the cache first — avoids file I/O + SVG parsing on repeated
    // requests for the same icon (e.g. during theme refresh).
    const QString key = cache_key(name, color);
    auto& cache = icon_cache();
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    QFile f(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    if (!f.open(QIODevice::ReadOnly)) return QIcon();

    QString svg = QString::fromUtf8(f.readAll());
    // Recolor: every "currentColor" occurrence becomes the target color so
    // both stroke-based and fill-based icons pick up the requested color.
    svg.replace(QStringLiteral("currentColor"), color.name());

    // Render via the SVG image-format plugin (shipped by qt6-svg-plugins),
    // which decodes the recolored SVG bytes into a QPixmap without requiring
    // a build-time link to Qt6::Svg. The SVGs declare a 24x24 viewport, so
    // the resulting pixmap is 24x24 and QIcon scales it on demand.
    QPixmap pm;
    if (!pm.loadFromData(svg.toUtf8(), "svg")) return QIcon();
    QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

} // namespace gui
