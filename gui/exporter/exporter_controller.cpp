// gui/exporter/exporter_controller.cpp

#include "exporter_controller.h"

#include <QMetaObject>

#include <algorithm>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>
#include <metavision/sdk/core/utils/colors.h>
#include <metavision/sdk/core/utils/cv_video_recorder.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/sdk/stream/file_config_hints.h>
#include <metavision/sdk/stream/hdf5_event_file_writer.h>

#include <opencv2/imgproc.hpp>

namespace gui {

ExporterController::ExporterController(QObject* parent) : QObject(parent) {}

ExporterController::~ExporterController() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

bool ExporterController::start(const ExportParams& params) {
    if (running_) return false;
    if (worker_.joinable()) worker_.join();  // reap previous finished worker
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this, params]() {
        try {
            if (params.format == ExportParams::Format::HDF5) {
                run_hdf5(params);
            } else {
                run_avi(params);
            }
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = QString::fromUtf8(e.what())]() {
                emit failed(msg);
            }, Qt::QueuedConnection);
        }
        running_ = false;
    });
    return true;
}

void ExporterController::cancel() {
    cancel_ = true;
}

void ExporterController::run_hdf5(const ExportParams& p) {
    Metavision::Camera cam;
    try {
        Metavision::FileConfigHints hints;
        hints.real_time_playback(false); // as fast as possible
        cam = Metavision::Camera::from_file(p.source_path.toStdString(), hints);
    } catch (const Metavision::CameraException& e) {
        QMetaObject::invokeMethod(this, [this, msg = QString::fromUtf8(e.what())]() {
            emit failed(msg);
        }, Qt::QueuedConnection);
        return;
    }

    // Query total duration once for progress reporting. The OSC is only
    // available for file sources, which is always the case for export.
    Metavision::timestamp dur_us = 0;
    try {
        auto& osc = cam.offline_streaming_control();
        if (osc.is_ready()) dur_us = osc.get_duration();
    } catch (...) {}

    Metavision::HDF5EventFileWriter writer(p.output_path.toStdString());
    std::atomic<Metavision::timestamp> last_ts{0};
    auto id = cam.cd().add_callback(
        [&writer, &last_ts, this](const Metavision::EventCD* b, const Metavision::EventCD* e) {
            try {
                if (cancel_) return;
                writer.add_events(b, e);
                if (b != e) last_ts.store((e - 1)->t, std::memory_order_relaxed);
            } catch (const std::exception&) {
                cancel_ = true;  // convert failure to graceful cancel
            } catch (...) {
                cancel_ = true;
            }
        });
    cam.start();
    // Spin until EOF or cancel. Emit incremental progress so the progress bar
    // does not sit at 0% for the entire export (only jumping to 100% at the end).
    while (!cancel_) {
        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!cam.is_running()) break;
            if (dur_us > 0) {
                const double r = std::min(1.0, static_cast<double>(
                    last_ts.load(std::memory_order_relaxed)) / dur_us);
                QMetaObject::invokeMethod(this, [this, r]() { emit progress(r); },
                                          Qt::QueuedConnection);
            }
        } catch (...) {
            break;
        }
    }
    try { cam.stop(); } catch (...) {}
    cam.cd().remove_callback(id);
    writer.close();

    // Distinguish cancel from completion: a cancelled export must not emit
    // completed (the output file is partial/truncated).
    if (cancel_) {
        QMetaObject::invokeMethod(this, [this]() {
            emit failed(tr("Export cancelled."));
        }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(this, [this, out = p.output_path]() {
        emit progress(1.0);
        emit completed(out);
    }, Qt::QueuedConnection);
}

void ExporterController::run_avi(const ExportParams& p) {
    Metavision::Camera cam;
    try {
        Metavision::FileConfigHints hints;
        hints.real_time_playback(false);
        cam = Metavision::Camera::from_file(p.source_path.toStdString(), hints);
    } catch (const Metavision::CameraException& e) {
        QMetaObject::invokeMethod(this, [this, msg = QString::fromUtf8(e.what())]() {
            emit failed(msg);
        }, Qt::QueuedConnection);
        return;
    }

    // Query total duration once for progress reporting.
    Metavision::timestamp dur_us = 0;
    try {
        auto& osc = cam.offline_streaming_control();
        if (osc.is_ready()) dur_us = osc.get_duration();
    } catch (...) {}

    const auto& g = cam.geometry();
    const int w = g.get_width();
    const int h = g.get_height();

    const int fourcc = (p.quality >= 50) ? cv::VideoWriter::fourcc('H', '2', '6', '4')
                                         : cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    Metavision::CvVideoRecorder recorder(p.output_path.toStdString(), fourcc,
                                         static_cast<uint32_t>(p.fps), cv::Size(w, h), p.color);
    if (!recorder.start()) {
        QMetaObject::invokeMethod(this, [this]() {
            emit failed(tr("Failed to open AVI writer."));
        }, Qt::QueuedConnection);
        return;
    }

    auto gen = std::make_unique<Metavision::CDFrameGenerator>(w, h);
    gen->set_color_palette(p.color ? Metavision::ColorPalette::Dark : Metavision::ColorPalette::Gray);
    gen->set_display_accumulation_time_us(
        static_cast<Metavision::timestamp>(p.accumulation_ms * 1000.0));

    std::atomic<bool> done{false};
    // start() takes (fps, callback). The callback receives (timestamp, frame).
    // It returns false if the generator thread failed to start.
    //
    // Color handling: CDFrameGenerator defaults to single-channel (grayscale)
    // output regardless of the palette; set_color_palette only selects the
    // lookup table, not the channel count. CvVideoRecorder was constructed
    // with p.color as its `colored` flag, so we must hand it a frame whose
    // channel count matches: 3-channel BGR when p.color, 1-channel gray
    // otherwise. cv::VideoWriter with isColor=false fed a 3-channel image
    // (or vice versa) produces corrupted output or an OpenCV assertion.
    if (!gen->start(static_cast<std::uint16_t>(p.fps),
                    [&recorder, &done, this, color = p.color](Metavision::timestamp, cv::Mat& frame) {
                        try {
                            if (cancel_ || done) return;
                            if (frame.empty()) return;
                            cv::Mat out;
                            if (color) {
                                if (frame.channels() == 1) {
                                    cv::cvtColor(frame, out, cv::COLOR_GRAY2BGR);
                                } else {
                                    out = frame;
                                }
                            } else {
                                if (frame.channels() == 3) {
                                    cv::cvtColor(frame, out, cv::COLOR_BGR2GRAY);
                                } else {
                                    out = frame;
                                }
                            }
                            recorder.write(out);
                        } catch (const std::exception&) {
                            cancel_ = true;  // convert failure to graceful cancel
                        } catch (...) {
                            cancel_ = true;
                        }
                    })) {
        QMetaObject::invokeMethod(this, [this]() {
            emit failed(tr("Failed to start CD frame generator."));
        }, Qt::QueuedConnection);
        recorder.stop();
        return;
    }

    std::atomic<Metavision::timestamp> last_ts{0};
    auto id = cam.cd().add_callback(
        [&gen, &last_ts, this](const Metavision::EventCD* b, const Metavision::EventCD* e) {
            try {
                gen->add_events(b, e);
                if (b != e) last_ts.store((e - 1)->t, std::memory_order_relaxed);
            } catch (const std::exception&) {
                cancel_ = true;  // convert failure to graceful cancel
            } catch (...) {
                cancel_ = true;
            }
        });

    cam.start();
    while (!cancel_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!cam.is_running()) break;
        if (dur_us > 0) {
            const double r = std::min(1.0, static_cast<double>(
                last_ts.load(std::memory_order_relaxed)) / dur_us);
            QMetaObject::invokeMethod(this, [this, r]() { emit progress(r); },
                                      Qt::QueuedConnection);
        }
    }
    done = true;
    try { cam.stop(); } catch (...) {}
    cam.cd().remove_callback(id);
    gen->stop();
    recorder.stop();

    if (cancel_) {
        QMetaObject::invokeMethod(this, [this]() {
            emit failed(tr("Export cancelled."));
        }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(this, [this, out = p.output_path]() {
        emit progress(1.0);
        emit completed(out);
    }, Qt::QueuedConnection);
}

} // namespace gui
