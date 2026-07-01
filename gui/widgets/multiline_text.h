// gui/widgets/multiline_text.h — multi-line text composition for HUD/counters.
//
// Design §1.6.6 (jAER MultilineAnnotationTextRenderer). A small header-only
// helper that composes a multi-line QString from labeled (key, value) pairs
// and can render it onto a QPainter with an optional semi-transparent panel
// background. Used for the stats overlay (event counter, FPS, event rate).
//
// Header-only.

#ifndef GUI_WIDGETS_MULTILINE_TEXT_H
#define GUI_WIDGETS_MULTILINE_TEXT_H

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace gui {

/// @brief Composes and renders multi-line labeled-value text (HUD overlay).
///
/// Inspired by jAER MultilineAnnotationTextRenderer. Insert ordered
/// (label, value) pairs, then either render the composed block onto a
/// QPainter (with an optional rounded background panel) or retrieve the
/// joined QString for use elsewhere (e.g. a QLabel).
class MultilineText {
public:
    MultilineText() = default;

    /// @brief Constructs with a preset font.
    explicit MultilineText(const QFont& font) : font_(font) {}

    /// @brief Sets the font used for measuring and rendering.
    void set_font(const QFont& font) { font_ = font; }
    const QFont& font() const { return font_; }

    /// @brief Sets the separator inserted between label and value.
    void set_separator(const QString& sep) { separator_ = sep; }
    const QString& separator() const { return separator_; }

    /// @brief Sets the text color.
    void set_text_color(const QColor& c) { text_color_ = c; }
    /// @brief Sets the background panel color (use invalid QColor to disable).
    void set_background(const QColor& c) { background_ = c; }

    /// @brief Appends a labeled value. The value is converted via QString::number.
    void add(const QString& label, double value) {
        entries_.emplace_back(label, QString::number(value));
    }
    /// @brief Appends a labeled value with explicit text (handles ints, etc.).
    void add(const QString& label, const QString& value) {
        entries_.emplace_back(label, value);
    }

    /// @brief Replaces the value of an existing label, or appends if absent.
    void set(const QString& label, const QString& value) {
        for (auto& e : entries_) {
            if (e.first == label) {
                e.second = value;
                return;
            }
        }
        entries_.emplace_back(label, value);
    }

    /// @brief Removes the entry with the given label (no-op if absent).
    void remove(const QString& label) {
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                           [&](const Entry& e) { return e.first == label; }),
            entries_.end());
    }

    /// @brief Clears all entries.
    void clear() { entries_.clear(); }

    /// @brief Number of currently held lines.
    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    /// @brief Composes the multi-line string (one "label: value" per line).
    QString to_string() const {
        QStringList lines;
        lines.reserve(static_cast<int>(entries_.size()));
        for (const auto& e : entries_) {
            lines << e.first + separator_ + e.second;
        }
        return lines.join('\n');
    }

    /// @brief Renders the composed text with top-left at @p origin.
    /// Renders the optional background panel sized to the text block plus
    /// @p padding on every side.
    void draw(QPainter& painter, const QPoint& origin, int padding = 4) const {
        if (entries_.empty()) return;
        painter.save();
        painter.setFont(font_);
        const QFontMetrics fm(font_);
        const QString text = to_string();
        const QRect text_rect = fm.boundingRect(QRect(origin, QSize(1, 1)),
                                                Qt::TextFlag::TextExpandTabs |
                                                    Qt::AlignLeft | Qt::AlignTop,
                                                text);

        QRect box = text_rect.adjusted(-padding, -padding, padding, padding);
        box.moveTopLeft(origin);

        if (background_.isValid()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(background_);
            painter.drawRoundedRect(box, 4, 4);
        }

        painter.setPen(QPen(text_color_));
        const QRect draw_text_rect = box.adjusted(padding, padding, -padding, -padding);
        painter.drawText(draw_text_rect,
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextExpandTabs,
                         text);
        painter.restore();
    }

    /// @brief Returns the bounding box (in painter coords) that draw() would
    /// occupy when anchored at @p origin. Useful for layout / hit-testing.
    QRect bounding_rect(const QPoint& origin, int padding = 4) const {
        if (entries_.empty()) return QRect(origin, QSize(0, 0));
        const QFontMetrics fm(font_);
        const QRect text_rect = fm.boundingRect(QRect(origin, QSize(1, 1)),
                                                Qt::TextFlag::TextExpandTabs |
                                                    Qt::AlignLeft | Qt::AlignTop,
                                                to_string());
        return text_rect.adjusted(-padding, -padding, padding, padding);
    }

private:
    using Entry = std::pair<QString, QString>;

    QFont font_{QFont("Monospace", 9)};
    QString separator_{": "};
    QColor text_color_{Qt::white};
    QColor background_{QColor(0, 0, 0, 140)};  // semi-transparent black
    std::vector<Entry> entries_;
};

} // namespace gui

#endif // GUI_WIDGETS_MULTILINE_TEXT_H
