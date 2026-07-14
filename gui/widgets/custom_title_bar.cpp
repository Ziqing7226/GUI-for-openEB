// gui/widgets/custom_title_bar.cpp

#include "custom_title_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QWindow>

#include "app/icon_provider.h"

namespace gui {

// ---------------------------------------------------------------------------
// CustomTitleBar
// ---------------------------------------------------------------------------

CustomTitleBar::CustomTitleBar(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(36);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(8);

    // Left cluster: [app name]. The title label gets the object name
    // "AppTitle" so QSS can target it. The rounded-rectangle background
    // (§13 — opposite-mode panel color) and the inverse title text color
    // are applied in setColors() so they track the active theme.
    title_label_ = new QLabel(QStringLiteral("EB plus"), this);
    title_label_->setObjectName(QStringLiteral("AppTitle"));
    title_label_->setAttribute(Qt::WA_StyledBackground, true);
    // Keep the chip tight: Fixed vertical policy prevents the label from
    // stretching to the full 36px bar height; AlignVCenter centers it.
    // Maximum horizontal policy prevents horizontal expansion beyond the
    // text + padding sizeHint.
    title_label_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    title_label_->setAlignment(Qt::AlignCenter);

    layout->addWidget(title_label_, 0, Qt::AlignVCenter);

    // Menu dropdown buttons. Populated via addMenu() from MainWindow's
    // build_menus().
    menu_layout_ = new QHBoxLayout();
    menu_layout_->setContentsMargins(0, 0, 0, 0);
    menu_layout_->setSpacing(2);
    layout->addLayout(menu_layout_);

    layout->addStretch(1);

    // Window control buttons (right side, flush against each other). The icon
    // names are stored so refresh_icons() can re-render them on theme change.
    const int btn_size = 36;
    auto make_btn = [this, btn_size](const QString& icon_name, const QString& tip) {
        auto* btn = new QPushButton(this);
        btn->setIcon(IconProvider::get(icon_name));
        btn->setFixedSize(btn_size, btn_size);
        btn->setToolTip(tip);
        btn->setCursor(Qt::ArrowCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; }"
            "QPushButton:hover { background-color: rgba(128,128,128,60); }"
            "QPushButton:pressed { background-color: rgba(128,128,128,100); }"));
        return btn;
    };

    min_icon_name_   = QStringLiteral("minimize");
    max_icon_name_   = QStringLiteral("maximize");
    close_icon_name_ = QStringLiteral("close");
    btn_min_   = make_btn(min_icon_name_,   tr("Minimize"));
    btn_max_   = make_btn(max_icon_name_,   tr("Maximize"));
    btn_close_ = make_btn(close_icon_name_, tr("Close"));

    connect(btn_min_, &QPushButton::clicked, this, [this]() {
        if (auto* w = window()) w->showMinimized();
    });
    connect(btn_max_, &QPushButton::clicked, this, [this]() {
        if (auto* w = window()) {
            if (w->isMaximized()) w->showNormal();
            else w->showMaximized();
        }
    });
    connect(btn_close_, &QPushButton::clicked, this, [this]() {
        if (auto* w = window()) w->close();
    });

    auto* ctrl_layout = new QHBoxLayout();
    ctrl_layout->setContentsMargins(0, 0, 0, 0);
    ctrl_layout->setSpacing(0);
    ctrl_layout->addWidget(btn_min_);
    ctrl_layout->addWidget(btn_max_);
    ctrl_layout->addWidget(btn_close_);
    layout->addLayout(ctrl_layout);
}

QMenu* CustomTitleBar::addMenu(const QString& title) {
    auto* btn = new QPushButton(title, this);
    btn->setCursor(Qt::ArrowCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; padding: 0 8px; }"
        "QPushButton:hover { background-color: rgba(128,128,128,60); }"
        "QPushButton:pressed { background-color: rgba(128,128,128,100); }"));
    auto* menu = new QMenu(title, btn);
    btn->setMenu(menu);
    menu_layout_->addWidget(btn);
    return menu;
}

void CustomTitleBar::setTitle(const QString& title) {
    if (title_label_) title_label_->setText(title);
}

void CustomTitleBar::refresh_icons() {
    if (btn_min_   && !min_icon_name_.isEmpty())
        btn_min_->setIcon(IconProvider::get(min_icon_name_));
    if (btn_max_   && !max_icon_name_.isEmpty())
        btn_max_->setIcon(IconProvider::get(max_icon_name_));
    if (btn_close_ && !close_icon_name_.isEmpty())
        btn_close_->setIcon(IconProvider::get(close_icon_name_));
}

void CustomTitleBar::setColors(const QColor& bg, const QColor& fg,
                               const QColor& title_fg, const QColor& title_box) {
    bg_color_ = bg;
    fg_color_ = fg;
    title_color_ = title_fg;
    title_box_color_ = title_box;

    // Separator line color — visible on both light and dark backgrounds:
    // dark bg → lighter line, light bg → darker line (§15.1).
    line_color_ = bg.lightness() < 128
        ? bg.lighter(140)
        : bg.darker(120);

    const QString bg_hex = bg.name();
    const QString fg_hex = fg.name();
    const QString title_hex = title_fg.name();
    const QString box_hex = title_box.name();
    const QString line_hex = line_color_.name();

    // §15.3: the background is painted manually in paintEvent() because the
    // global QSS rule `QWidget { background-color: bg-primary; }` overrides
    // the local `CustomTitleBar { ... }` rule in Qt's stylesheet cascade for
    // widgets installed via setMenuWidget().  The QSS here only styles child
    // widgets (labels, buttons, menus).
    //
    // §13: the title label "EB plus" gets a rounded-rectangle chip background
    // (title_box — the opposite-mode panel color) with the title text color
    // (RGB-inverse of title_box). The chip is styled inline on the label
    // because the global QSS `QLabel` rule would otherwise override the
    // background. Placeholder numbering is sequential (%1/%2/%3) to avoid
    // ambiguity in Qt's arg() chaining.
    setStyleSheet(QStringLiteral(
        "QLabel { color: %2; background: transparent; border: none; }"
        "QPushButton { color: %2; background: transparent; border: none; }"
        "QPushButton#qt_menubar_ext_button { background: transparent; border: none; }"
        "QMenu { background-color: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background-color: rgba(128,128,128,60); }"
        "QMenu::separator { height: 1px; background: %3; margin: 2px 6px; }"
    ).arg(bg_hex, fg_hex, line_hex));

    // Apply the title chip styling inline so it wins over the global QSS
    // `QLabel { background: transparent; }` rule (Qt gives inline
    // setStyleSheet() higher priority in the cascade). Padding is tight so
    // the chip just covers the text; border-radius gives uniform rounded
    // corners on all four sides.
    if (title_label_) {
        title_label_->setStyleSheet(QStringLiteral(
            "color: %1; background-color: %2; "
            "border-radius: 4px; padding: 3px 5px; font-weight: bold;"
        ).arg(title_hex, box_hex));
    }

    // Re-render window control icons so they track the new theme.
    refresh_icons();
    update();
}

void CustomTitleBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // Fill the title bar background — painted manually to bypass the global
    // QSS `QWidget { background-color: bg-primary; }` which overrides the
    // local stylesheet for setMenuWidget()-installed widgets (§15.3).
    p.fillRect(rect(), bg_color_);
    // Draw the bottom separator line (§15.1).
    if (line_color_.isValid()) {
        p.setPen(QPen(line_color_, 1));
        p.drawLine(rect().bottomLeft(), rect().bottomRight());
    }
}

void CustomTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Ask the WM to handle the drag — this is the same mechanism
        // VSCode uses via -webkit-app-region: drag.
        if (auto* w = window()) {
            if (auto* handle = w->windowHandle()) {
                handle->startSystemMove();
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* w = window()) {
            if (w->isMaximized()) w->showNormal();
            else w->showMaximized();
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
// ResizeGrip
// ---------------------------------------------------------------------------

ResizeGrip::ResizeGrip(Position pos, QWidget* parent)
    : QWidget(parent), pos_(pos) {
    // Transparent, stays on top, no focus.
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoMousePropagation, true);
    setFocusPolicy(Qt::NoFocus);

    switch (pos_) {
        case Position::Left:
        case Position::Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case Position::Top:
        case Position::Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Position::TopLeft:
        case Position::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Position::TopRight:
        case Position::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
    }
}

void ResizeGrip::reposition(const QRect& r) {
    const int e = kEdgeThickness;
    const int c = kCornerSize;
    switch (pos_) {
        case Position::Left:         setGeometry(0, c, e, r.height() - 2 * c); break;
        case Position::Right:        setGeometry(r.width() - e, c, e, r.height() - 2 * c); break;
        case Position::Top:          setGeometry(c, 0, r.width() - 2 * c, e); break;
        case Position::Bottom:       setGeometry(c, r.height() - e, r.width() - 2 * c, e); break;
        case Position::TopLeft:      setGeometry(0, 0, c, c); break;
        case Position::TopRight:     setGeometry(r.width() - c, 0, c, c); break;
        case Position::BottomLeft:   setGeometry(0, r.height() - c, c, c); break;
        case Position::BottomRight:  setGeometry(r.width() - c, r.height() - c, c, c); break;
    }
    raise();
}

void ResizeGrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (auto* w = window()) {
        if (auto* handle = w->windowHandle()) {
            Qt::Edges edges;
            switch (pos_) {
                case Position::Left:        edges = Qt::LeftEdge; break;
                case Position::Right:       edges = Qt::RightEdge; break;
                case Position::Top:         edges = Qt::TopEdge; break;
                case Position::Bottom:      edges = Qt::BottomEdge; break;
                case Position::TopLeft:     edges = Qt::TopEdge    | Qt::LeftEdge;  break;
                case Position::TopRight:    edges = Qt::TopEdge    | Qt::RightEdge; break;
                case Position::BottomLeft:  edges = Qt::BottomEdge | Qt::LeftEdge;  break;
                case Position::BottomRight: edges = Qt::BottomEdge | Qt::RightEdge; break;
            }
            handle->startSystemResize(edges);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

} // namespace gui
