// gui/widgets/target_labeler.h — dataset labeling tool.
//
// Design §1.3 and §1.6.2 (jAER TargetLabeler). A QWidget that lets the user
// draw bounding boxes over an event-frame snapshot and assign each a class
// label, then persist the annotations to JSON. Intended to overlay (or be
// fed a snapshot from) the EventDisplayWidget.
//
// Each label record stores: class_id, bbox (sensor-pixel rect), timestamp
// (μs of the captured frame), and frame_ref (an integer the caller uses to
// associate the label with a captured frame / file offset).

#ifndef GUI_WIDGETS_TARGET_LABELER_H
#define GUI_WIDGETS_TARGET_LABELER_H

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include <cstdint>
#include <vector>

class QPaintEvent;
class QMouseEvent;

namespace gui {

class TargetLabeler : public QWidget {
    Q_OBJECT
public:
    /// @brief A single annotated bounding box.
    struct Label {
        int class_id{0};
        QRect bbox;            ///< sensor-pixel rectangle
        qint64 timestamp{0};   ///< μs timestamp of the captured frame
        int frame_ref{0};      ///< caller-defined frame identifier
    };

    explicit TargetLabeler(QWidget* parent = nullptr);
    ~TargetLabeler();

    /// @brief Sets the frame snapshot to label. The widget resizes to fit.
    void set_image(const QImage& image);

    /// @brief Sets the timestamp + frame_ref that will be attached to labels
    /// drawn from now on.
    void set_context(qint64 timestamp_us, int frame_ref);

    /// @brief Sets the list of known class names (drives the pick dialog).
    void set_class_names(const std::vector<QString>& names);
    const std::vector<QString>& class_names() const { return class_names_; }

    /// @brief Current labels (read-only view).
    const std::vector<Label>& labels() const { return labels_; }

    /// @brief Removes the label under @p pos (widget coords). Returns true
    /// if one was removed.
    bool remove_at(const QPoint& pos);

public slots:
    /// @brief Clears all labels (keeps the image).
    void clear_labels();
    /// @brief Undoes the last drawn label.
    void undo_last();
    /// @brief Saves labels to a JSON file. Returns true on success.
    bool save_to_json(const QString& path);
    /// @brief Loads labels from a JSON file (appends to current labels).
    /// Returns true on success.
    bool load_from_json(const QString& path);

signals:
    void labels_changed();
    void error_message(const QString& msg);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int pick_class_id();
    QRect normalized_rect(const QPoint& a, const QPoint& b) const;

    QImage image_;
    std::vector<Label> labels_;
    std::vector<QString> class_names_;

    bool drawing_{false};
    QPoint drag_start_;
    QPoint drag_curr_;

    qint64 ctx_timestamp_{0};
    int ctx_frame_ref_{0};
};

} // namespace gui

#endif // GUI_WIDGETS_TARGET_LABELER_H
