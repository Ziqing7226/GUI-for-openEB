// gui/widgets/custom_title_bar.cpp

#include "custom_title_bar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QWindow>

namespace gui {

// ---------------------------------------------------------------------------
// CustomTitleBar
// ---------------------------------------------------------------------------

CustomTitleBar::CustomTitleBar(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(32);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Title label (center) — created before the buttons so layout order is
    // [menu_bar] [stretch] [title] [stretch] [min] [max] [close].
    // menu_bar_ is inserted at index 0 later via setMenuBar().
    title_label_ = new QLabel(this);
    title_label_->setAlignment(Qt::AlignCenter);
    title_label_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    // Window control buttons (right side).
    const int btn_size = 32;
    auto make_btn = [this, btn_size](QStyle::StandardPixmap pix, const QString& tip) {
        auto* btn = new QPushButton(this);
        btn->setIcon(style()->standardIcon(pix));
        btn->setFixedSize(btn_size, btn_size);
        btn->setToolTip(tip);
        btn->setCursor(Qt::ArrowCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };

    btn_min_   = make_btn(QStyle::SP_TitleBarMinButton,  tr("Minimize"));
    btn_max_   = make_btn(QStyle::SP_TitleBarMaxButton,  tr("Maximize"));
    btn_close_ = make_btn(QStyle::SP_TitleBarCloseButton, tr("Close"));

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

    layout->addStretch(1);
    layout->addWidget(title_label_, 0, Qt::AlignCenter);
    layout->addStretch(1);
    layout->addWidget(btn_min_);
    layout->addWidget(btn_max_);
    layout->addWidget(btn_close_);
}

void CustomTitleBar::setMenuBar(QMenuBar* menu_bar) {
    if (!menu_bar) return;
    menu_bar_ = menu_bar;
    menu_bar_->setParent(this);
    auto* layout = qobject_cast<QHBoxLayout*>(this->layout());
    if (layout) {
        layout->insertWidget(0, menu_bar_);
    }
}

void CustomTitleBar::setTitle(const QString& title) {
    if (title_label_) title_label_->setText(title);
}

void CustomTitleBar::setColors(const QColor& bg, const QColor& fg) {
    bg_color_ = bg;
    fg_color_ = fg;

    const QString bg_hex = bg.name();
    const QString fg_hex = fg.name();

    // Style the title bar itself, the embedded menu bar, and the window
    // control buttons.  The buttons are transparent so the title bar
    // background shows through; on hover they get a subtle overlay.
    setStyleSheet(QStringLiteral(
        "CustomTitleBar { background-color: %1; }"
        "QMenuBar { background-color: %1; color: %2; }"
        "QMenuBar::item { background-color: transparent; padding: 4px 10px; }"
        "QMenuBar::item:selected { background-color: rgba(128,128,128,60); }"
        "QMenu { background-color: %1; color: %2; border: 1px solid #888; }"
        "QMenu::item:selected { background-color: rgba(128,128,128,80); }"
        "QLabel { color: %2; background: transparent; border: none; }"
        "QPushButton { background-color: transparent; border: none; }"
        "QPushButton:hover { background-color: rgba(128,128,128,60); }"
        "QPushButton:pressed { background-color: rgba(128,128,128,100); }"
    ).arg(bg_hex, fg_hex));

    update();
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

void CustomTitleBar::paintEvent(QPaintEvent* /*event*/) {
    if (!bg_color_.isValid()) return;
    QPainter p(this);
    p.fillRect(rect(), bg_color_);
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
