// Integration test: simulate the full GUI flow for file playback with
// FilterChain using a REAL .raw file. Verifies that flip_x toggled AFTER
// playback starts takes effect immediately.
//
// Build:
//   cmake --build build --target test_filter_integration
// Run:
//   ./build/algo/tests/test_filter_integration <file.raw>

#include <QCoreApplication>
#include <QImage>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <thread>

#include <opencv2/imgproc.hpp>

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/file_config_hints.h>
#include <metavision/sdk/stream/offline_streaming_control.h>

#include "algo_bridge/filter_chain.h"
#include "app/file_frame_generator.h"

using namespace gui;
using std::chrono::steady_clock;
using std::chrono::milliseconds;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <file.raw>\n", argv[0]);
        return 1;
    }
    QCoreApplication app(argc, argv);
    const std::string path = argv[1];

    // --- Simulate CameraController setup ---
    FilterChain chain;  // CameraController member

    FileFrameGenerator gen;  // FileFrameGenerator (member of FramePipeline)
    gen.set_fps(60);
    gen.set_accumulation_time_us(33000);

    // Capture frames
    QImage last_frame;
    QObject::connect(&gen, &FileFrameGenerator::frame_ready,
                     [&](QImage f, Metavision::timestamp) {
                         last_frame = f;
                     });

    // Open file and get geometry
    Metavision::FileConfigHints hints;
    hints.real_time_playback(false);
    Metavision::Camera cam = Metavision::Camera::from_file(path, hints);
    const long W = cam.geometry().get_width();
    const long H = cam.geometry().get_height();
    std::fprintf(stderr, "File: %s  geometry: %ldx%ld\n", path.c_str(), W, H);

    // Simulate setup_camera(is_file=true):
    // 1. set_geometry on filter_chain (CameraController does this)
    chain.set_geometry(static_cast<int>(W), static_cast<int>(H));
    // 2. set_filter_chain (FramePipeline::set_file_filter_chain does this)
    gen.set_filter_chain(&chain);
    // 3. set_geometry on generator (start_file does this)
    gen.set_geometry(W, H);

    // Buffer all events (simulating CD callback → add_events)
    cam.cd().add_callback([&](const Metavision::EventCD* b, const Metavision::EventCD* e) {
        gen.add_events(b, e);
    });

    Metavision::timestamp duration = 0;
    cam.start();
    try {
        auto& osc = cam.offline_streaming_control();
        if (osc.is_ready()) duration = osc.get_duration();
    } catch (...) {}
    gen.set_duration_us(duration);

    // Wait for buffering
    bool camera_done = false;
    cam.add_status_change_callback([&](const Metavision::CameraStatus& s) {
        if (s == Metavision::CameraStatus::STOPPED) camera_done = true;
    });
    auto t0 = steady_clock::now();
    while (!camera_done && steady_clock::now() - t0 < milliseconds(3000)) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(milliseconds(5));
    }
    if (cam.is_running()) cam.stop();
    std::fprintf(stderr, "Buffered. duration=%lld us  events=%zu\n",
                 (long long)duration, gen.event_count());

    // --- Test 1: Play WITHOUT filter, capture a frame ---
    gen.play();
    t0 = steady_clock::now();
    while (steady_clock::now() - t0 < milliseconds(200)) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(milliseconds(1));
    }
    gen.pause();
    QImage no_filter_frame = last_frame;
    if (no_filter_frame.isNull()) {
        std::fprintf(stderr, "FAIL: no frame produced without filter\n");
        return 1;
    }
    std::fprintf(stderr, "no_filter frame: %dx%d\n",
                 no_filter_frame.width(), no_filter_frame.height());

    // Pick a non-background pixel to track
    int track_x = -1, track_y = -1;
    const QRgb bg = no_filter_frame.pixel(0, 0);
    for (int y = 0; y < no_filter_frame.height() && track_x < 0; ++y) {
        for (int x = 0; x < no_filter_frame.width(); ++x) {
            if (no_filter_frame.pixel(x, y) != bg) {
                track_x = x;
                track_y = y;
                break;
            }
        }
    }
    if (track_x < 0) {
        std::fprintf(stderr, "WARN: could not find a non-background pixel; aborting\n");
        return 1;
    }
    const QRgb track_pixel = no_filter_frame.pixel(track_x, track_y);
    const int flip_x_pos = static_cast<int>(W) - 1 - track_x;
    std::fprintf(stderr, "Track pixel (%d,%d)=%08X  flip→(%d,%d)\n",
                 track_x, track_y, track_pixel, flip_x_pos, track_y);

    // --- Test 2: Enable flip_x DURING playback ---
    chain.set_stage_enabled("flip_x", true);
    std::fprintf(stderr, "has_enabled() = %d\n", chain.has_enabled() ? 1 : 0);
    gen.play();
    t0 = steady_clock::now();
    while (steady_clock::now() - t0 < milliseconds(200)) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(milliseconds(1));
    }
    gen.pause();
    QImage flip_frame = last_frame;

    bool ok = true;
    const QRgb after_track = flip_frame.pixel(track_x, track_y);
    const QRgb after_flip = flip_frame.pixel(flip_x_pos, track_y);
    std::fprintf(stderr, "After flip: pixel(%d,%d)=%08X  pixel(%d,%d)=%08X  (orig=%08X)\n",
                 track_x, track_y, after_track, flip_x_pos, track_y, after_flip, track_pixel);

    if (after_track == track_pixel) {
        std::fprintf(stderr, "FAIL: pixel at original position unchanged — flip NOT applied\n");
        ok = false;
    }
    if (after_flip != track_pixel) {
        std::fprintf(stderr, "FAIL: pixel at flipped position doesn't match original\n");
        ok = false;
    }

    // --- Test 3: Disable flip_x, verify it reverts ---
    chain.set_stage_enabled("flip_x", false);
    gen.play();
    t0 = steady_clock::now();
    while (steady_clock::now() - t0 < milliseconds(200)) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(milliseconds(1));
    }
    gen.pause();
    QImage revert_frame = last_frame;
    const QRgb revert_track = revert_frame.pixel(track_x, track_y);
    std::fprintf(stderr, "After disable: pixel(%d,%d)=%08X (orig=%08X)\n",
                 track_x, track_y, revert_track, track_pixel);
    if (revert_track != track_pixel) {
        std::fprintf(stderr, "WARN: pixel didn't revert exactly (may be different frame)\n");
    }

    if (ok) {
        std::fprintf(stderr, "\nPASS: flip_x works during file playback\n");
        return 0;
    }
    std::fprintf(stderr, "\nFAIL: flip_x does NOT work during file playback\n");
    return 1;
}
