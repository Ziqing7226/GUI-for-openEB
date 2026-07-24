// gui/calibration/chessboard_display.h — full-screen flashing chessboard for
// event-camera intrinsic calibration.
//
// The board inverts at 20 Hz (one flip every 50 ms). Every flip generates a
// burst of events along the chessboard edges; the CalibrationWizard's 1 ms
// accumulator picks the burst window out of each 50 ms cycle and feeds it to
// OpenCV corner detection — so the camera "sees" the chessboard for free,
// without any APS frame grabber.
//
// Board geometry is computed from the widget's own rect() (resizeEvent), so
// fullscreen and windowed modes are both correct (audit §9.2-B / §六-B5 —
// the previous code computed origin from the screen's availableGeometry but
// painted in widget coordinates, misplacing the board in windowed mode).
// Only the physical-DPI lookup for square_size_mm still uses the screen.
//
// Rendering: squares are drawn directly in paintEvent via fillRect — no
// pre-rendered full-screen pixmaps. On each flip, update() is scoped to the
// board rect only, so Qt recomposites just the board area (~10–20 % of a 4K
// surface) instead of the full screen. This keeps the 20 Hz flip loop cheap
// enough to stay responsive on large / high-DPI displays.

#ifndef GUI_CALIBRATION_CHESSBOARD_DISPLAY_H
#define GUI_CALIBRATION_CHESSBOARD_DISPLAY_H

#include <QPointer>
#include <QWidget>

class QScreen;
class QTimer;

namespace gui {

class ChessboardDisplay : public QWidget {
    Q_OBJECT
public:
    explicit ChessboardDisplay(QWidget* parent = nullptr);
    ~ChessboardDisplay();

    /// @brief Moves the window to @p screen and recomputes board geometry
    /// for that screen's resolution + DPI. Pass nullptr to use the primary
    /// screen.
    void attach_to_screen(QScreen* screen);

    /// @brief Sets the inner-corner count (cols, rows). Recomputes geometry.
    void set_pattern(int inner_cols, int inner_rows);

    /// @brief Starts/stops the 20 Hz flip timer.
    void start_flicker();
    void stop_flicker();

    /// @brief Inner-corner count the board was built for (OpenCV convention).
    int inner_cols() const { return inner_cols_; }
    int inner_rows() const { return inner_rows_; }
    /// @brief Computed square side in millimeters (from screen DPI).
    float square_size_mm() const { return square_size_mm_; }
    /// @brief Computed square side in pixels.
    int square_size_px() const { return square_size_px_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void recompute_geometry();

    int inner_cols_{9};
    int inner_rows_{6};
    int square_size_px_{0};
    float square_size_mm_{0.0f};
    int board_origin_x_{0};
    int board_origin_y_{0};
    int board_w_px_{0};
    int board_h_px_{0};

    bool inverted_{false};
    bool fullscreen_{true};  ///< Defaults to fullscreen — the board must fill
                              ///< the screen for the camera to see it whole.
    QTimer* timer_{nullptr};
    QPointer<QScreen> attached_screen_;  ///< QPointer (audit §六-B6): a hot-
                                         ///< unplugged screen must not dangle.
};

} // namespace gui

#endif // GUI_CALIBRATION_CHESSBOARD_DISPLAY_H
