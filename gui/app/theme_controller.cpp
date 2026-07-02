// gui/app/theme_controller.cpp

#include "theme_controller.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QColor>

namespace gui {

namespace {

/// Light-mode background colors. The defaults are soft pastels so that
/// black text remains readable on every choice.
constexpr const char* kLightColors[] = {
    "#E8E8E8",  // Light Gray (default)
    "#D7EBD4",  // Light Green
    "#FDF4D2",  // Light Yellow
    "#F8D7DC",  // Light Pink
    "#D4E6F1",  // Light Blue
};

/// Dark-mode background colors — each is the dark variant of the
/// corresponding light color above. The user's color choice applies to BOTH
/// light and dark mode, so dark mode is not a single black but a
/// color-tinted dark background.
constexpr const char* kDarkColors[] = {
    "#2D2D30",  // Dark Gray (default)
    "#1E3A22",  // Dark Green
    "#3D3520",  // Dark Yellow (dark amber/olive)
    "#3D2030",  // Dark Pink (dark rose)
    "#1E2A3D",  // Dark Blue (dark navy)
};

/// Returns true if Qt exposes a system color-scheme hint (Qt 6.5+).
/// Sets @p dark_out to the dark-mode flag when available.
bool query_system_dark(bool& dark_out) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        const auto scheme = app->styleHints()->colorScheme();
        dark_out = (scheme == Qt::ColorScheme::Dark);
        return true;
    }
#else
    (void)dark_out;
#endif
    return false;
}

} // namespace

ThemeController::ThemeController(QObject* parent) : QObject(parent) {
    load_settings();

    // Query the initial system color scheme (best-effort).
    (void)query_system_dark(system_is_dark_);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        connect(app->styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { on_system_color_scheme_changed(); });
    }
#endif
}

void ThemeController::set_target(QMainWindow* window) {
    window_ = window;
    apply_stylesheet();
}

void ThemeController::set_color(Color c) {
    if (c == color_) return;
    color_ = c;
    save_settings();
    apply_stylesheet();
    sync_menu_actions();
    emit theme_changed();
}

void ThemeController::set_mode(Mode m) {
    if (m == mode_) return;
    mode_ = m;
    save_settings();
    apply_stylesheet();
    sync_menu_actions();
    emit theme_changed();
}

QString ThemeController::color_name(Color c) {
    switch (c) {
        case Color::LightGray:   return tr("Gray");
        case Color::LightGreen:  return tr("Green");
        case Color::LightYellow: return tr("Yellow");
        case Color::LightPink:   return tr("Pink");
        case Color::LightBlue:   return tr("Blue");
    }
    return tr("Gray");
}

QString ThemeController::mode_name(Mode m) {
    switch (m) {
        case Mode::FollowSystem: return tr("Follow System");
        case Mode::AlwaysLight:  return tr("Light");
        case Mode::AlwaysDark:   return tr("Dark");
    }
    return tr("Follow System");
}

QString ThemeController::effective_background_hex() const {
    bool dark = false;
    switch (mode_) {
        case Mode::FollowSystem: dark = system_is_dark_; break;
        case Mode::AlwaysLight:  dark = false; break;
        case Mode::AlwaysDark:   dark = true; break;
    }
    const int idx = static_cast<int>(color_);
    const int count = static_cast<int>(sizeof(dark ? kDarkColors : kLightColors) / sizeof(const char*));
    if (idx < 0 || idx >= count) {
        return dark ? QString::fromLatin1(kDarkColors[0])
                    : QString::fromLatin1(kLightColors[0]);
    }
    return dark ? QString::fromLatin1(kDarkColors[idx])
                : QString::fromLatin1(kLightColors[idx]);
}

QString ThemeController::effective_text_hex() const {
    QColor bg(effective_background_hex());
    if (!bg.isValid()) return QStringLiteral("#000000");
    // Standard relative-luminance threshold (W3C-style approximation).
    const int luminance = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue());
    return (luminance < 128) ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");
}

void ThemeController::build_menu(QMenu* parent_menu) {
    if (!parent_menu) return;
    menu_ = parent_menu;

    // --- Color submenu ---
    auto* color_menu = menu_->addMenu(tr("Color"));
    color_group_ = new QActionGroup(menu_);
    color_group_->setExclusive(true);
    color_actions_.clear();
    for (int i = 0; i <= static_cast<int>(Color::LightBlue); ++i) {
        const auto c = static_cast<Color>(i);
        auto* a = color_menu->addAction(color_name(c));
        a->setCheckable(true);
        a->setChecked(color_ == c);
        color_group_->addAction(a);
        connect(a, &QAction::triggered, this, [this, c]() { set_color(c); });
        color_actions_.push_back(a);
    }

    // --- Mode submenu ---
    auto* mode_menu = menu_->addMenu(tr("Mode"));
    mode_group_ = new QActionGroup(menu_);
    mode_group_->setExclusive(true);
    mode_actions_.clear();
    for (int i = 0; i <= static_cast<int>(Mode::AlwaysDark); ++i) {
        const auto m = static_cast<Mode>(i);
        auto* a = mode_menu->addAction(mode_name(m));
        a->setCheckable(true);
        a->setChecked(mode_ == m);
        mode_group_->addAction(a);
        connect(a, &QAction::triggered, this, [this, m]() { set_mode(m); });
        mode_actions_.push_back(a);
    }
}

void ThemeController::sync_menu_actions() {
    for (size_t i = 0; i < color_actions_.size(); ++i) {
        if (color_actions_[i]) {
            QSignalBlocker b(color_actions_[i]);
            color_actions_[i]->setChecked(static_cast<int>(color_) == static_cast<int>(i));
        }
    }
    for (size_t i = 0; i < mode_actions_.size(); ++i) {
        if (mode_actions_[i]) {
            QSignalBlocker b(mode_actions_[i]);
            mode_actions_[i]->setChecked(static_cast<int>(mode_) == static_cast<int>(i));
        }
    }
}

void ThemeController::load_settings() {
    QSettings s;
    // Default color: Blue. Default mode: Follow System.
    color_ = static_cast<Color>(s.value("theme/color", static_cast<int>(Color::LightBlue)).toInt());
    mode_  = static_cast<Mode>(s.value("theme/mode",  static_cast<int>(Mode::FollowSystem)).toInt());
    // Clamp to valid enum ranges to guard against corrupted settings.
    if (color_ < Color::LightGray || color_ > Color::LightBlue) color_ = Color::LightBlue;
    if (mode_  < Mode::FollowSystem || mode_  > Mode::AlwaysDark)  mode_  = Mode::FollowSystem;
}

void ThemeController::save_settings() const {
    QSettings s;
    s.setValue("theme/color", static_cast<int>(color_));
    s.setValue("theme/mode",  static_cast<int>(mode_));
}

void ThemeController::apply_stylesheet() {
    if (!window_) return;
    const QString bg = effective_background_hex();
    const QString fg = effective_text_hex();

    QColor base(bg);
    // Input/alternative shades derived from the base for both light & dark.
    QColor input_bg = base.lighter(112);
    if (input_bg == base) input_bg = base.darker(112);
    QColor alt_bg = base.darker(108);
    if (alt_bg == base) alt_bg = base.lighter(108);
    // Title bar (QMenuBar) shade — slightly darker than the main background
    // in light mode, slightly lighter in dark mode, so the title bar reads
    // as a distinct visual band (VSCode pattern).
    const int lum = (0.299 * base.red() + 0.587 * base.green() + 0.114 * base.blue());
    QColor title_bg = (lum < 128) ? base.lighter(125) : base.darker(115);
    if (title_bg == base) title_bg = (lum < 128) ? base.lighter(115) : base.darker(120);

    const QString input_hex = input_bg.isValid() ? input_bg.name() : bg;
    const QString alt_hex = alt_bg.isValid() ? alt_bg.name() : bg;
    const QString title_hex = title_bg.isValid() ? title_bg.name() : alt_hex;

    const QString qss = QStringLiteral(
        "QMainWindow, QWidget { background-color: %1; color: %2; }"
        "QDockWidget, QTabWidget, QScrollArea, QGroupBox { background-color: %1; color: %2; }"
        "QDockWidget::title { background-color: %4; padding: 4px; }"
        "QTabBar::tab { background-color: %3; color: %2; padding: 4px 12px; border: 1px solid #888; }"
        "QTabBar::tab:selected { background-color: %1; }"
        "QPushButton { background-color: %3; color: %2; border: 1px solid #888; padding: 3px 10px; border-radius: 2px; }"
        "QPushButton:hover { background-color: %4; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QSlider { background-color: %3; color: %2; border: 1px solid #888; padding: 2px; }"
        "QCheckBox, QRadioButton, QLabel { color: %2; }"
        "QToolBar { background-color: %4; border: none; spacing: 2px; padding: 2px; }"
        "QStatusBar { background-color: %4; color: %2; }"
        "QMenuBar { background-color: %5; color: %2; }"
        "QMenuBar::item:selected { background-color: %3; }"
        "QMenu { background-color: %4; color: %2; border: 1px solid #888; }"
        "QMenu::item:selected { background-color: %3; }"
        "QListWidget, QTreeWidget, QTableWidget { background-color: %3; color: %2; }"
        "QHeaderView::section { background-color: %4; color: %2; padding: 2px; border: 1px solid #888; }"
    ).arg(bg, fg, input_hex, alt_hex, title_hex);
    window_->setStyleSheet(qss);

    // Also set the application palette so that palette-derived widgets pick
    // up the theme.
    QPalette pal;
    pal.setColor(QPalette::Window, base);
    pal.setColor(QPalette::WindowText, QColor(fg));
    pal.setColor(QPalette::Base, input_bg);
    pal.setColor(QPalette::AlternateBase, alt_bg);
    pal.setColor(QPalette::Text, QColor(fg));
    pal.setColor(QPalette::Button, alt_bg);
    pal.setColor(QPalette::ButtonText, QColor(fg));
    pal.setColor(QPalette::ToolTipBase, alt_bg);
    pal.setColor(QPalette::ToolTipText, QColor(fg));
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->setPalette(pal);
    }
}

void ThemeController::on_system_color_scheme_changed() {
    bool dark = false;
    if (!query_system_dark(dark)) return;
    if (dark == system_is_dark_) return;
    system_is_dark_ = dark;
    if (mode_ == Mode::FollowSystem) {
        apply_stylesheet();
        sync_menu_actions();
        emit theme_changed();
    }
}

} // namespace gui
