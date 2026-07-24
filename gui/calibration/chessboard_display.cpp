// gui/calibration/chessboard_display.cpp

#include "chessboard_display.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>

namespace gui {

namespace {
// Flip interval = 50 ms → 20 inversions/sec. Each 50 ms cycle aligns with
// the CalibrationWizard's capture cycle so every cycle contains exactly one
// edge burst from the flip.
constexpr int kFlipIntervalMs = 50;
} // namespace

ChessboardDisplay::ChessboardDisplay(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(tr("Calibration Chessboard"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAutoFillBackground(false);
    // We paint every pixel of the dirty rect ourselves (black fill + squares),
    // so Qt can skip the background fill — saves a full-screen memset on each
    // repaint of a 4K surface.
    setAttribute(Qt::WA_OpaquePaintEvent);
    // Black background so the area outside the board doesn't generate
    // spurious edge events during flips.
    setPalette(Qt::black);

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(kFlipIntervalMs);
    connect(timer_, &QTimer::timeout, this, [this]() {
        inverted_ = !inverted_;
        // Only repaint the board area — the hint overlay doesn't change
        // between flips, and the screen area outside the board stays black.
        // This is the critical performance win: a full-screen update() on a
        // 4K surface at 20 Hz forces ~660 MB/s of backing-store compositing;
        // scoping the update to the board rect cuts that by the screen-to-board
        // area ratio.
        update(QRect(board_origin_x_, board_origin_y_, board_w_px_, board_h_px_));
    });

    attach_to_screen(QGuiApplication::primaryScreen());
    resize(800, 600);
}

ChessboardDisplay::~ChessboardDisplay() {
    if (timer_) timer_->stop();
}

void ChessboardDisplay::attach_to_screen(QScreen* screen) {
    attached_screen_ = screen ? screen : QGuiApplication::primaryScreen();
    recompute_geometry();
    if (attached_screen_) {
        // Park the window at the screen's top-left so showFullScreen() lands
        // on the right monitor; user can toggle windowed mode with F.
        move(attached_screen_->geometry().topLeft());
    }
    update();  // full repaint — geometry changed
}

void ChessboardDisplay::set_pattern(int inner_cols, int inner_rows) {
    inner_cols_ = std::max(2, inner_cols);
    inner_rows_ = std::max(2, inner_rows);
    recompute_geometry();
    update();  // full repaint — pattern changed
}

void ChessboardDisplay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Geometry follows the widget's own rect (audit §9.2-B / §六-B5), so
    // fullscreen↔windowed transitions recompute automatically. The resize
    // itself already schedules a repaint — no explicit update() needed.
    recompute_geometry();
}

void ChessboardDisplay::recompute_geometry() {
    // Available drawing area: the widget's own rect — the same coordinate
    // system paintEvent draws in — so the board is always exactly where it
    // is painted, in both fullscreen and windowed mode.
    const int avail_w = width();
    const int avail_h = height();
    if (avail_w <= 0 || avail_h <= 0) return;

    // The board has (inner_cols+1) × (inner_rows+1) squares. Choose a square
    // size that fills ~90% of the smaller axis so corners span a wide range
    // of the field of view — improves calibration conditioning.
    const int squares_w = inner_cols_ + 1;
    const int squares_h = inner_rows_ + 1;
    const int sq = std::min(avail_w / squares_w, avail_h / squares_h);
    square_size_px_ = std::max(8, static_cast<int>(sq * 0.9));
    board_w_px_ = square_size_px_ * squares_w;
    board_h_px_ = square_size_px_ * squares_h;
    board_origin_x_ = (avail_w - board_w_px_) / 2;
    board_origin_y_ = (avail_h - board_h_px_) / 2;

    // Physical size from screen DPI. QScreen::physicalDotsPerInch() reports
    // the panel's physical DPI; square_mm = pixels / dpi * 25.4. This value
    // is only a default — X11 DPI is often unreliable (audit §9.2-G).
    qreal dpi = 0.0;
    if (attached_screen_) dpi = attached_screen_->physicalDotsPerInch();
    if (dpi <= 0.0) dpi = 96.0;  // Fallback: typical desktop DPI.
    square_size_mm_ = static_cast<float>(square_size_px_ / dpi * 25.4);
}

void ChessboardDisplay::start_flicker() {
    if (!timer_->isActive()) timer_->start();
}

void ChessboardDisplay::stop_flicker() {
    timer_->stop();
}

void ChessboardDisplay::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect dirty = event->rect();

    // Fill the dirty region with black. This handles both the area outside
    // the board and the board's black squares in one memset. Combined with
    // WA_OpaquePaintEvent, Qt skips its own background fill, so this is the
    // only full pass over the dirty pixels.
    p.fillRect(dirty, Qt::black);

    // Draw only the white squares that intersect the dirty region. The black
    // squares are already filled above, so we skip them. This is cheap: a
    // 9×6 inner-corner board has ≤70 squares total, and on a flip only the
    // board rect is dirty, so every square is checked but only fills are
    // issued for white ones.
    if (square_size_px_ > 0) {
        for (int r = 0; r <= inner_rows_; ++r) {
            for (int c = 0; c <= inner_cols_; ++c) {
                const bool is_white = ((r + c) % 2 == 0) ^ inverted_;
                if (!is_white) continue;
                const QRect sq(board_origin_x_ + c * square_size_px_,
                               board_origin_y_ + r * square_size_px_,
                               square_size_px_, square_size_px_);
                if (sq.intersects(dirty)) {
                    p.fillRect(sq, Qt::white);
                }
            }
        }
    }

    // Hint overlay (small, top-left). Only redraw if the hint area is in the
    // dirty region — on a flip the dirty region is the board rect, which
    // normally doesn't overlap the top-left hint, so this is skipped.
    const QRect hint_rect(12, 8, width() - 24, 28);
    if (hint_rect.intersects(dirty)) {
        p.setPen(QPen(QColor(255, 200, 0)));
        QFont f = p.font();
        f.setPointSize(10);
        p.setFont(f);
        QString hint = tr("F: fullscreen  |  Esc: close  |  %1×%2 inner corners  |  square = %3 mm")
                           .arg(inner_cols_).arg(inner_rows_)
                           .arg(square_size_mm_, 0, 'f', 1);
        if (fullscreen_) hint = tr("F: windowed  |  Esc: close  |  %1×%2  |  square = %3 mm")
                                     .arg(inner_cols_).arg(inner_rows_)
                                     .arg(square_size_mm_, 0, 'f', 1);
        p.drawText(hint_rect, Qt::AlignLeft | Qt::AlignTop, hint);
    }
}

void ChessboardDisplay::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_F:
            fullscreen_ = !fullscreen_;
            if (fullscreen_) {
                showFullScreen();
            } else {
                showNormal();
            }
            break;
        case Qt::Key_Escape:
            close();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

} // namespace gui