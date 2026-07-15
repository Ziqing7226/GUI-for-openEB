// gui/display/display_strategy.h — display-strategy polymorphism (design §3.5).
//
// Each AlgoDisplayMode gets a concrete IDisplayStrategy that knows how to
// render an AlgoResult onto the main frame or route it to a child window.
// AlgoInstance owns the strategy selected at construction time; MainWindow
// just iterates live instances and calls apply_strategy(), collapsing the
// former switch-based process_algo_results() (§3.5.4).
//
// The strategies reproduce the exact per-mode behavior that previously lived
// in the switch branches of MainWindow::process_algo_results(); they must not
// change what is drawn, only how it is dispatched.

#ifndef GUI_DISPLAY_DISPLAY_STRATEGY_H
#define GUI_DISPLAY_DISPLAY_STRATEGY_H

#include <QHash>
#include <QImage>
#include <QPointer>
#include <string>

namespace cv { class Mat; }

namespace gui {

class FrameAnnotator;
class AlgoWindow;
class SpaceTimeDisplay;
class MainWindow;
class CameraController;
class AlgoInstance;
struct AlgoInfo;
struct AlgoResult;

/// Shared state handed to each strategy by MainWindow::process_algo_results().
/// MainWindow pulls the latest AlgoResult for an instance, fills this context
/// with pointers to its own display members, then calls
/// AlgoInstance::apply_strategy(). Strategies read/write widgets through these
/// pointers; @p window is used as the QObject context for
/// QMetaObject::invokeMethod(..., Qt::QueuedConnection) so widget touches are
/// marshalled to the GUI thread exactly as the old switch-based code did.
struct DisplayContext {
    FrameAnnotator* annotator;
    QHash<std::string, QPointer<AlgoWindow>>* algo_windows;
    SpaceTimeDisplay* xyt_display;
    MainWindow* window;
    CameraController* camera;
    AlgoInstance* instance;  ///< Set by AlgoInstance::apply_strategy() before dispatch.
};

/// Abstract display strategy. apply() mutates @p frame in place (Overlay /
/// Replace) or routes the result to a child window (Standalone / Passive).
class IDisplayStrategy {
public:
    virtual ~IDisplayStrategy() = default;
    virtual void apply(QImage& frame, AlgoResult& result,
                       const AlgoInfo& info, DisplayContext& ctx) = 0;
};

/// Passive: no drawing; only updates the AlgoWindow status text if one is open.
class PassiveStrategy final : public IDisplayStrategy {
public:
    void apply(QImage& frame, AlgoResult& result,
               const AlgoInfo& info, DisplayContext& ctx) override;
};

/// Overlay: draws boxes/lines/points/circles/texts/colored_events/trajectories
/// on the main frame via FrameAnnotator; optionally routes an aux frame and a
/// ROI zoom crop to the AlgoWindow's display widget.
class OverlayStrategy final : public IDisplayStrategy {
public:
    void apply(QImage& frame, AlgoResult& result,
               const AlgoInfo& info, DisplayContext& ctx) override;
};

/// Replace: substitutes the main display frame with the algorithm's output.
class ReplaceStrategy final : public IDisplayStrategy {
public:
    void apply(QImage& frame, AlgoResult& result,
               const AlgoInfo& info, DisplayContext& ctx) override;
};

/// Standalone: routes the result frame + status text to the AlgoWindow.
class StandaloneStrategy final : public IDisplayStrategy {
public:
    void apply(QImage& frame, AlgoResult& result,
               const AlgoInfo& info, DisplayContext& ctx) override;
};

/// Converts a cv::Mat (1-channel gray or 3-channel BGR) to a deep-copied
/// RGB888 QImage suitable for display. Shared by Replace/Standalone/Overlay
/// strategies (moved out of main_window.cpp's anonymous namespace, §3.5.4).
QImage mat_to_qimage(const cv::Mat& mat);

} // namespace gui

#endif // GUI_DISPLAY_DISPLAY_STRATEGY_H
