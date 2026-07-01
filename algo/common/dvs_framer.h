// algo/common/dvs_framer.h — DVS event framer (pure ON/OFF count accumulation).
//
// Inspired by jAER DvsFramer. Accumulates per-pixel ON/OFF event counts over a
// configurable accumulation window and produces a grayscale cv::Mat. This is
// the non-DL reconstruction baseline: brightness ~ event count. Polarity is
// encoded as signed accumulation (ON brightens, OFF darkens) or as separate
// channels depending on mode.

#ifndef GUI_ALGO_COMMON_DVS_FRAMER_H
#define GUI_ALGO_COMMON_DVS_FRAMER_H

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include <metavision/sdk/base/utils/timestamp.h>
#include <metavision/sdk/base/events/event_cd.h>

#include "event.h"

namespace gui_algo {

/// @brief DVS-style event framer producing grayscale frames from ON/OFF counts.
class DvsFramer {
public:
    enum class PolarityMode {
        /// ON and OFF both brighten the pixel (event-count image).
        UnsignedCount,
        /// ON brightens, OFF darkens (signed accumulation → bipolar image).
        Signed,
        /// Two-channel output: channel 0 = OFF count, channel 1 = ON count.
        SplitPolarity,
    };

    DvsFramer(int width, int height, PolarityMode mode = PolarityMode::UnsignedCount)
        : width_(width), height_(height), mode_(mode) {
        reset();
    }

    /// @brief Accumulates events in [begin, end) into the current frame.
    template <typename Iter>
    void add_events(Iter begin, Iter end) {
        for (auto it = begin; it != end; ++it) {
            add_one(static_cast<std::uint16_t>(it->x),
                    static_cast<std::uint16_t>(it->y),
                    static_cast<short>(it->p));
        }
    }

    /// @brief Accumulates a single SDK event.
    void add_event(const Metavision::EventCD& e) {
        add_one(e.x, e.y, e.p);
    }

    /// @brief Generates the current accumulated frame and resets the accumulator.
    /// @param accumulation_time_us Accumulation window (recorded as metadata;
    ///        actual windowing is driven by caller invoking reset() periodically).
    /// @return cv::Mat of type CV_8UC1 (UnsignedCount/Signed) or CV_8UC2
    ///         (SplitPolarity). Counts are saturated to [0, 255].
    cv::Mat generate_and_reset(Metavision::timestamp accumulation_time_us = 0) {
        (void)accumulation_time_us;
        cv::Mat frame;
        switch (mode_) {
            case PolarityMode::UnsignedCount: {
                // ON + OFF both brighten the pixel (total event-count image).
                frame.create(height_, width_, CV_8UC1);
                for (int i = 0; i < width_ * height_; ++i) {
                    const int v = on_counts_[i] + off_counts_[i];
                    frame.at<std::uint8_t>(i) =
                        static_cast<std::uint8_t>(v > 255 ? 255 : v);
                }
                break;
            }
            case PolarityMode::Signed: {
                frame.create(height_, width_, CV_8UC1);
                for (int i = 0; i < width_ * height_; ++i) {
                    const int v = 128 + on_counts_[i] - off_counts_[i];
                    frame.at<std::uint8_t>(i) =
                        static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                break;
            }
            case PolarityMode::SplitPolarity: {
                frame.create(height_, width_, CV_8UC2);
                for (int i = 0; i < width_ * height_; ++i) {
                    cv::Vec2b& px = frame.at<cv::Vec2b>(i);
                    px[0] = off_counts_[i];
                    px[1] = on_counts_[i];
                }
                break;
            }
        }
        reset();
        return frame;
    }

    /// @brief Generates the current accumulated frame without resetting.
    cv::Mat generate(Metavision::timestamp accumulation_time_us = 0) {
        (void)accumulation_time_us;
        cv::Mat frame;
        switch (mode_) {
            case PolarityMode::UnsignedCount: {
                // ON + OFF both brighten the pixel (total event-count image).
                frame.create(height_, width_, CV_8UC1);
                for (int i = 0; i < width_ * height_; ++i) {
                    const int v = on_counts_[i] + off_counts_[i];
                    frame.at<std::uint8_t>(i) =
                        static_cast<std::uint8_t>(v > 255 ? 255 : v);
                }
                break;
            }
            case PolarityMode::Signed: {
                frame.create(height_, width_, CV_8UC1);
                for (int i = 0; i < width_ * height_; ++i) {
                    const int v = 128 + on_counts_[i] - off_counts_[i];
                    frame.at<std::uint8_t>(i) =
                        static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                break;
            }
            case PolarityMode::SplitPolarity: {
                frame.create(height_, width_, CV_8UC2);
                for (int i = 0; i < width_ * height_; ++i) {
                    cv::Vec2b& px = frame.at<cv::Vec2b>(i);
                    px[0] = off_counts_[i];
                    px[1] = on_counts_[i];
                }
                break;
            }
        }
        return frame;
    }

    void reset() {
        on_counts_.assign(static_cast<std::size_t>(width_) * height_, 0);
        off_counts_.assign(static_cast<std::size_t>(width_) * height_, 0);
    }

    int width() const { return width_; }
    int height() const { return height_; }
    PolarityMode mode() const { return mode_; }

private:
    void add_one(std::uint16_t x, std::uint16_t y, short p) {
        if (x >= width_ || y >= height_) return;
        const std::size_t idx = static_cast<std::size_t>(y) * width_ + x;
        if (p) {
            if (on_counts_[idx] < 255) ++on_counts_[idx];
        } else {
            if (off_counts_[idx] < 255) ++off_counts_[idx];
        }
    }

    int width_;
    int height_;
    PolarityMode mode_;
    std::vector<std::uint8_t> on_counts_;
    std::vector<std::uint8_t> off_counts_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_DVS_FRAMER_H
