// gui/widgets/target_labeler.cpp

#include "target_labeler.h"

#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QSaveFile>

#include <algorithm>

namespace gui {

TargetLabeler::TargetLabeler(QWidget* parent) : QWidget(parent) {
    setWindowTitle(tr("Target Labeler"));
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    // Default class palette.
    class_names_ = {tr("object"), tr("person"), tr("vehicle"), tr("marker")};
}

TargetLabeler::~TargetLabeler() = default;

void TargetLabeler::set_image(const QImage& image) {
    image_ = image;
    if (!image_.isNull()) {
        const QSize s = image_.size();
        setMinimumSize(s.width() / 2, s.height() / 2);
    }
    update();
}

void TargetLabeler::set_context(qint64 timestamp_us, int frame_ref) {
    ctx_timestamp_ = timestamp_us;
    ctx_frame_ref_ = frame_ref;
}

void TargetLabeler::set_class_names(const std::vector<QString>& names) {
    class_names_ = names;
    if (class_names_.empty()) {
        class_names_ = {tr("object")};
    }
}

void TargetLabeler::clear_labels() {
    labels_.clear();
    emit labels_changed();
    update();
}

void TargetLabeler::undo_last() {
    if (!labels_.empty()) {
        labels_.pop_back();
        emit labels_changed();
        update();
    }
}

bool TargetLabeler::remove_at(const QPoint& pos) {
    for (auto it = labels_.rbegin(); it != labels_.rend(); ++it) {
        if (it->bbox.contains(pos)) {
            labels_.erase(std::next(it).base());
            emit labels_changed();
            update();
            return true;
        }
    }
    return false;
}

QRect TargetLabeler::normalized_rect(const QPoint& a, const QPoint& b) const {
    const int x = std::min(a.x(), b.x());
    const int y = std::min(a.y(), b.y());
    const int w = std::abs(a.x() - b.x());
    const int h = std::abs(a.y() - b.y());
    return QRect(x, y, w, h);
}

int TargetLabeler::pick_class_id() {
    QStringList items;
    for (int i = 0; i < static_cast<int>(class_names_.size()); ++i) {
        items << QString("%1: %2").arg(i).arg(class_names_[i]);
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Assign Class"), tr("Class label:"), items, 0, false, &ok);
    if (!ok || choice.isEmpty()) return -1;
    const int idx = choice.section(':', 0, 0).toInt(&ok);
    if (!ok || idx < 0 || idx >= static_cast<int>(class_names_.size())) return -1;
    return idx;
}

void TargetLabeler::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        if (event->button() == Qt::RightButton) {
            remove_at(event->position().toPoint());
        }
        return;
    }
    drawing_ = true;
    drag_start_ = event->position().toPoint();
    drag_curr_ = drag_start_;
}

void TargetLabeler::mouseMoveEvent(QMouseEvent* event) {
    if (!drawing_) return;
    drag_curr_ = event->position().toPoint();
    update();
}

void TargetLabeler::mouseReleaseEvent(QMouseEvent* event) {
    if (!drawing_) return;
    drawing_ = false;
    drag_curr_ = event->position().toPoint();
    const QRect rect = normalized_rect(drag_start_, drag_curr_);
    if (rect.width() < 3 || rect.height() < 3) {
        update();
        return;  // ignore tiny drags (clicks)
    }
    const int class_id = pick_class_id();
    if (class_id < 0) {
        update();
        return;
    }
    Label lbl;
    lbl.class_id = class_id;
    lbl.bbox = rect;
    lbl.timestamp = ctx_timestamp_;
    lbl.frame_ref = ctx_frame_ref_;
    labels_.push_back(lbl);
    emit labels_changed();
    update();
}

void TargetLabeler::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background: draw the frame snapshot scaled to widget, else dark fill.
    if (!image_.isNull()) {
        p.drawImage(rect(), image_);
    } else {
        p.fillRect(rect(), QColor(30, 30, 30));
    }

    // Persisted labels.
    QPen pen(Qt::yellow, 2);
    p.setPen(pen);
    p.setFont(QFont("Monospace", 9));
    static const QColor kColors[] = {
        QColor(255, 80, 80), QColor(80, 255, 80), QColor(80, 180, 255),
        QColor(255, 200, 0), QColor(255, 0, 255), QColor(0, 255, 255),
    };
    for (const auto& lbl : labels_) {
        const QColor c = kColors[lbl.class_id % 6];
        pen.setColor(c);
        p.setPen(pen);
        p.setBrush(QBrush(c, Qt::BDiagPattern));
        p.drawRect(lbl.bbox);
        const QString tag = QString("#%1 %2")
                                .arg(lbl.class_id)
                                .arg(lbl.class_id < static_cast<int>(class_names_.size())
                                         ? class_names_[lbl.class_id]
                                         : QStringLiteral("?"));
        p.fillRect(lbl.bbox.left(), lbl.bbox.top() - 14,
                   p.fontMetrics().horizontalAdvance(tag) + 6, 14, c);
        p.setPen(QPen(Qt::black));
        p.drawText(lbl.bbox.left() + 3, lbl.bbox.top() - 3, tag);
    }

    // In-progress drag rectangle.
    if (drawing_) {
        const QRect r = normalized_rect(drag_start_, drag_curr_);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }
}

bool TargetLabeler::save_to_json(const QString& path) {
    QJsonArray arr;
    for (const auto& lbl : labels_) {
        QJsonObject o;
        o["class_id"] = lbl.class_id;
        o["frame_ref"] = lbl.frame_ref;
        o["timestamp_us"] = static_cast<qint64>(lbl.timestamp);
        QJsonObject bbox;
        bbox["x"] = lbl.bbox.x();
        bbox["y"] = lbl.bbox.y();
        bbox["w"] = lbl.bbox.width();
        bbox["h"] = lbl.bbox.height();
        o["bbox"] = bbox;
        if (lbl.class_id >= 0 && lbl.class_id < static_cast<int>(class_names_.size())) {
            o["class_name"] = class_names_[lbl.class_id];
        }
        arr.append(o);
    }
    QJsonObject root;
    root["labels"] = arr;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        emit error_message(tr("Cannot open %1 for writing.").arg(path));
        return false;
    }
    const QJsonDocument doc(root);
    f.write(doc.toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        emit error_message(tr("Failed to write %1.").arg(path));
        return false;
    }
    return true;
}

bool TargetLabeler::load_from_json(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit error_message(tr("Cannot open %1 for reading.").arg(path));
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        emit error_message(tr("Invalid JSON in %1.").arg(path));
        return false;
    }
    const QJsonArray arr = doc.object().value("labels").toArray();
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        Label lbl;
        lbl.class_id = o.value("class_id").toInt(0);
        lbl.frame_ref = o.value("frame_ref").toInt(0);
        lbl.timestamp = static_cast<qint64>(o.value("timestamp_us").toVariant().toLongLong());
        const QJsonObject b = o.value("bbox").toObject();
        lbl.bbox = QRect(b.value("x").toInt(), b.value("y").toInt(),
                         b.value("w").toInt(), b.value("h").toInt());
        labels_.push_back(lbl);
    }
    emit labels_changed();
    update();
    return true;
}

} // namespace gui
