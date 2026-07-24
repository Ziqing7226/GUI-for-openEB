// gui/app/camera_controller.h — owns the Metavision::Camera lifecycle.
//
// Discovers, connects (live or file), wires the CD callback into the
// FramePipeline and StatisticsController, and exposes sensor metadata to the
// GUI. All cross-thread communication goes through queued Qt signals.

#ifndef GUI_APP_CAMERA_CONTROLLER_H
#define GUI_APP_CAMERA_CONTROLLER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <metavision/hal/facilities/i_antiflicker_module.h>
#include <metavision/hal/facilities/i_erc_module.h>
#include <metavision/hal/facilities/i_event_trail_filter_module.h>
#include <metavision/hal/facilities/i_geometry.h>
#include <metavision/hal/facilities/i_ll_biases.h>
#include <metavision/hal/facilities/i_roi.h>
#include <metavision/hal/facilities/i_trigger_in.h>
#include <metavision/hal/facilities/i_trigger_out.h>
#include <metavision/sdk/base/utils/callback_id.h>
#include <metavision/sdk/stream/camera.h>

#include "frame_pipeline.h"
#include "statistics_controller.h"
#include "algo_bridge/filter_chain.h"

namespace gui {

// HAL facility aliases used by Phase 2 panels. Each is obtained via
// Camera::get_device().get_facility<T>() which returns a nullable pointer;
// panels must nullptr-check before use (graceful degradation when the
// connected sensor doesn't support a feature).
namespace facility {
using Biases       = Metavision::I_LL_Biases;
using Roi          = Metavision::I_ROI;
using AntiFlicker  = Metavision::I_AntiFlickerModule;
using TrailFilter  = Metavision::I_EventTrailFilterModule;
using Erc          = Metavision::I_ErcModule;
using TriggerIn    = Metavision::I_TriggerIn;
using TriggerOut   = Metavision::I_TriggerOut;
using Geometry     = Metavision::I_Geometry;
} // namespace facility

/// @brief Snapshot of sensor metadata shown in the Information panel.
struct SensorInfo {
    int width{0};
    int height{0};
    QString serial;
    QString integrator;
    QString plugin_name;
    QString encoding_format;
    QString firmware_version;
    QString generation_name;
    short generation_major{0};
    short generation_minor{0};
    bool is_file{false};
};

class CameraController : public QObject {
    Q_OBJECT
public:
    explicit CameraController(QObject* parent = nullptr);
    ~CameraController();

    /// @brief Lists online camera sources as (type_label, serial) pairs.
    std::vector<std::pair<QString, QString>> list_online_sources();

    /// @brief Connects to the first available live camera. Returns false on failure.
    bool connect_first_available();
    /// @brief Connects to a camera by serial number.
    bool connect_serial(const std::string& serial);
    /// @brief Opens an event file (RAW / HDF5 / DAT) for playback. Always
    /// uses real_time_playback=false so all events are read as fast as
    /// possible and buffered in the FileFrameGenerator. Playback rate is
    /// controlled by the FileFrameGenerator's QTimer (fps * window / 1e6).
    bool connect_file(const std::string& path);

    void disconnect();

    bool start();
    bool stop();
    bool is_running() const;
    bool is_connected() const { return static_cast<bool>(camera_); }
    bool is_file_source() const { return is_file_; }

    /// @brief Returns the underlying Metavision::Camera (nullptr if none).
    Metavision::Camera* camera_handle() { return camera_.get(); }

    const SensorInfo& sensor_info() const { return sensor_info_; }
    FramePipeline* frame_pipeline() { return &frame_pipeline_; }
    StatisticsController* statistics() { return &statistics_; }
    FilterChain* filter_chain() { return &filter_chain_; }

    /// @brief Phase 2 facility accessors. Each returns nullptr when no camera
    /// is connected or the connected sensor does not support that feature.
    /// Panels must nullptr-check before invoking any method on the returned
    /// pointer. The pointer is only valid until the next disconnect()/connect.
    facility::Biases*      biases_facility();
    facility::Roi*         roi_facility();
    facility::AntiFlicker* anti_flicker_facility();
    facility::TrailFilter* trail_filter_facility();
    facility::Erc*         erc_facility();
    facility::TriggerIn*   trigger_in_facility();
    facility::TriggerOut*  trigger_out_facility();

    /// @brief Reference-counted control of the cd_events_ready() broadcast
    /// (audit §11.1-B4 — hardening beyond develop's shared boolean). Each
    /// consumer (calibration wizard, sharpness dialog) acquires on start and
    /// releases on stop; the broadcast runs while at least one reference is
    /// held, so the two tools can be open concurrently without switching
    /// each other's stream off. When the count is 0 (default), the CD
    /// callback takes the fast path with zero extra copies. Must be called
    /// from the GUI thread (all current consumers do); release is clamped
    /// at 0 so an unbalanced call cannot wrap the count negative.
    void acquire_cd_broadcast();
    void release_cd_broadcast();

signals:
    void connected(const SensorInfo& info);
    void disconnected();
    void started();
    void stopped();
    void error(const QString& message);
    void runtime_warning(const QString& message);
    /// @brief Emitted from the SDK CD callback (cross-thread, queued) when
    /// cd_broadcast_ is true. Carries a shared_ptr copy of the batch so
    /// listeners on the GUI thread can process it safely. Used by the
    /// calibration wizard's 1 ms event accumulator.
    void cd_events_ready(std::shared_ptr<std::vector<Metavision::EventCD>> events);

private:
    /// @brief Sets up callbacks + pipeline for a new camera. Calls
    /// frame_pipeline_.start_file() for file sources (FileFrameGenerator)
    /// or frame_pipeline_.start() for live sources (CDFrameGenerator).
    void setup_camera(Metavision::Camera&& cam, bool is_file);
    /// @brief Tears down camera + callbacks + frame pipeline.
    void teardown();
    void fetch_sensor_info();

    std::unique_ptr<Metavision::Camera> camera_;
    std::optional<Metavision::CallbackId> cd_cb_id_;
    std::optional<Metavision::CallbackId> err_cb_id_;
    std::optional<Metavision::CallbackId> status_cb_id_;
    SensorInfo sensor_info_;
    bool is_file_{false};

    FramePipeline frame_pipeline_;
    StatisticsController statistics_;
    FilterChain filter_chain_;
    /// @brief When true, the CD callback emits cd_events_ready() with a copy
    /// of every batch. Off by default so non-calibration usage pays nothing.
    /// This is the SDK-thread-visible mirror of (cd_broadcast_refs_ > 0).
    std::atomic<bool> cd_broadcast_{false};
    /// @brief Number of consumers holding a CD broadcast reference
    /// (acquire_cd_broadcast/release_cd_broadcast). GUI thread only — the
    /// SDK thread reads the atomic cd_broadcast_ mirror instead.
    int cd_broadcast_refs_{0};
};

} // namespace gui

#endif // GUI_APP_CAMERA_CONTROLLER_H
