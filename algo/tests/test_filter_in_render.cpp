// Quick test: verify FilterChain is applied in FileFrameGenerator::render_frame()
// AND that events_window_ready emits filtered events (so algorithm output is
// also flipped when a Replace-mode algorithm is running).
#include <QCoreApplication>
#include <cstdio>
#include <opencv2/imgproc.hpp>

#include <metavision/sdk/base/events/event_cd.h>

#include "algo_bridge/filter_chain.h"
#include "app/file_frame_generator.h"

using namespace gui;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const int W = 100, H = 100;
    FilterChain chain;
    chain.set_geometry(W, H);

    FileFrameGenerator gen;
    gen.set_geometry(W, H);
    gen.set_filter_chain(&chain);
    gen.set_fps(60);
    gen.set_accumulation_time_us(1000);

    // Capture the rendered frame
    QImage rendered;
    QObject::connect(&gen, &FileFrameGenerator::frame_ready,
                     [&](QImage f, Metavision::timestamp) { rendered = f; });

    // Capture events_window_ready
    std::shared_ptr<std::vector<Metavision::EventCD>> emitted_events;
    QObject::connect(&gen, &FileFrameGenerator::events_window_ready,
                     [&](std::shared_ptr<std::vector<Metavision::EventCD>> evs,
                         Metavision::timestamp) { emitted_events = evs; });

    // Add a single event at (10, 50, p=1, t=100)
    Metavision::EventCD ev{10, 50, 100, 1};
    gen.add_events(&ev, &ev + 1);
    gen.set_duration_us(2000);

    // --- Test 1: No filter ---
    gen.seek(0);
    if (rendered.isNull()) {
        std::fprintf(stderr, "FAIL: no frame rendered\n");
        return 1;
    }
    const QRgb no_filter_pixel = rendered.pixel(10, 50);
    std::fprintf(stderr, "No filter: pixel(10,50) = %08X\n", no_filter_pixel);
    if (!emitted_events || emitted_events->size() != 1) {
        std::fprintf(stderr, "FAIL: events_window_ready didn't emit 1 event\n");
        return 1;
    }
    if ((*emitted_events)[0].x != 10) {
        std::fprintf(stderr, "FAIL: emitted event x=%d (expected 10)\n",
                     (*emitted_events)[0].x);
        return 1;
    }

    // --- Test 2: Enable flip_x ---
    // With flip_x, x=10 → W-1-10 = 89
    chain.set_stage_enabled("flip_x", true);
    gen.seek(0);
    if (rendered.isNull()) {
        std::fprintf(stderr, "FAIL: no frame rendered with flip_x\n");
        return 1;
    }
    const QRgb flip_pixel_at_10 = rendered.pixel(10, 50);
    const QRgb flip_pixel_at_89 = rendered.pixel(89, 50);
    std::fprintf(stderr, "Flip X: pixel(10,50) = %08X, pixel(89,50) = %08X\n",
                 flip_pixel_at_10, flip_pixel_at_89);

    bool ok = true;
    if (flip_pixel_at_10 == no_filter_pixel) {
        std::fprintf(stderr, "FAIL: pixel(10,50) unchanged after flip_x — filter NOT applied\n");
        ok = false;
    }
    if (flip_pixel_at_89 != no_filter_pixel) {
        std::fprintf(stderr, "FAIL: pixel(89,50) should match pre-flip pixel(10,50)\n");
        ok = false;
    }
    // Verify events_window_ready also emits FLIPPED events
    if (!emitted_events || emitted_events->size() != 1) {
        std::fprintf(stderr, "FAIL: events_window_ready didn't emit 1 event with flip\n");
        ok = false;
    } else if ((*emitted_events)[0].x != 89) {
        std::fprintf(stderr, "FAIL: emitted event x=%d (expected 89 after flip_x)\n",
                     (*emitted_events)[0].x);
        ok = false;
    } else {
        std::fprintf(stderr, "PASS: events_window_ready emitted flipped event x=89\n");
    }

    if (ok) {
        std::fprintf(stderr, "PASS: flip_x correctly applied in render_frame() and events_window_ready\n");
        return 0;
    }
    return 1;
}
