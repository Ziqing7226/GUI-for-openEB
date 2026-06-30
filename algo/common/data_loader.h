// algo/common/data_loader.h — RAW / HDF5 / DAT event file loader.
//
// Wraps Metavision::Camera::from_file to provide a uniform callback-based
// access to the event stream plus metadata and (for seekable files) random
// access. Used by offline analysis algorithms and the file-playback path.

#ifndef GUI_ALGO_COMMON_DATA_LOADER_H
#define GUI_ALGO_COMMON_DATA_LOADER_H

#include <optional>
#include <string>
#include <memory>

#include <metavision/sdk/base/utils/callback_id.h>
#include <metavision/sdk/base/utils/timestamp.h>
#include <metavision/sdk/stream/camera.h>

namespace gui_algo {

class DataLoader {
public:
    using EventCallback =
        std::function<void(const Metavision::EventCD*, const Metavision::EventCD*)>;

    DataLoader();
    ~DataLoader();

    DataLoader(const DataLoader&) = delete;
    DataLoader& operator=(const DataLoader&) = delete;

    /// @brief Opens an event file. Returns true on success.
    bool open(const std::string& path);
    void close();

    bool is_open() const { return static_cast<bool>(camera_); }

    /// @brief Registers a callback for CD events.
    /// @return Callback id, or empty optional on failure.
    std::optional<Metavision::CallbackId> set_event_callback(const EventCallback& cb);
    void clear_event_callback();

    /// @brief Starts playback (non-blocking; the SDK runs its own thread).
    bool start();
    bool stop();
    bool is_running() const;

    // File-only controls (no-op for live cameras).
    Metavision::timestamp duration() const;
    Metavision::timestamp last_timestamp() const;
    bool seek(Metavision::timestamp t);
    bool seekable() const;

    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& serial() const { return serial_; }
    const std::string& integrator() const { return integrator_; }
    const std::string& plugin_name() const { return plugin_; }

private:
    std::unique_ptr<Metavision::Camera> camera_;
    std::optional<Metavision::CallbackId> cd_cb_id_;
    int width_{0};
    int height_{0};
    std::string serial_;
    std::string integrator_;
    std::string plugin_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_DATA_LOADER_H
