// algo/tests/noise_tester.h — noise filter evaluation framework (header-only).
//
// Phase 10 (design §4.6.1): port of jAER NoiseTesterFilter. Generates
// synthetic signal events (ground-truth motion), injects Poisson background
// activity noise and signal dropouts, runs a candidate NoiseFilter, then
// scores TP/FP/TN/FN and derived precision / recall / F1 metrics.
//
// Usage:
//   NoiseTester tester;
//   tester.set_signal_generator(NoiseTester::Mode::Line);
//   tester.generate_signal_events(100000, 128, 128);
//   tester.inject_poisson_noise(1000.0f, 128, 128);
//   tester.inject_dropout(0.1f);
//   NoiseFilter filter(NoiseFilter::Mode::BAF, 128, 128);
//   tester.run_filter(filter);
//   auto m = tester.compute_metrics();

#ifndef GUI_ALGO_TESTS_NOISE_TESTER_H
#define GUI_ALGO_TESTS_NOISE_TESTER_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "algo/common/event.h"
#include "algo/tests/signal_noise_event.h"

namespace gui_algo {

/// @brief Forward declaration of the filter under test (algo/cv/noise_filter.h).
/// The concrete NoiseFilter must expose `bool process(const Event&)` returning
/// true when the event is accepted (passes) and false when filtered out.
class NoiseFilter;

/// @brief Noise filter evaluation framework (design §4.6.1).
class NoiseTester {
public:
    /// @brief Synthetic signal motion type.
    enum class Mode {
        Line,      ///< Linear translation across the sensor.
        Curve,     ///< Curved (sinusoidal) trajectory.
        Rotation   ///< Rotational motion about the image centre.
    };

    /// @brief Confusion-matrix metrics produced by compute_metrics().
    struct Metrics {
        int tp{0};   ///< True positives: signal events accepted.
        int fp{0};   ///< False positives: noise events accepted.
        int tn{0};   ///< True negatives: noise events rejected.
        int fn{0};   ///< False negatives: signal events rejected.
        float precision{0.0f};  ///< TP / (TP + FP)
        float recall{0.0f};     ///< TP / (TP + FN)
        float f1{0.0f};         ///< 2 * P * R / (P + R)
    };

    NoiseTester() : rng_(0xC0FFEEu) {}
    explicit NoiseTester(std::uint32_t seed) : rng_(seed) {}

    /// @brief Selects the synthetic signal generator shape.
    void set_signal_generator(Mode mode) { mode_ = mode; }

    /// @brief Generates ground-truth signal events (is_signal=true).
    /// @param duration_us Length of the synthetic stream in microseconds.
    /// @param width  Sensor width in pixels.
    /// @param height Sensor height in pixels.
    /// @return Number of signal events generated.
    std::size_t generate_signal_events(Metavision::timestamp duration_us,
                                       int width, int height) {
        events_.clear();
        const int cx = width / 2;
        const int cy = height / 2;
        const int radius = std::min(width, height) / 2 - 1;
        // ~1 event per microsecond of motion, stepping by 1us.
        for (Metavision::timestamp t = 0; t < duration_us; ++t) {
            std::uint16_t x = 0;
            std::uint16_t y = 0;
            short p = static_cast<short>(t & 1);
            switch (mode_) {
                case Mode::Line: {
                    // Horizontal sweep wrapping at width.
                    x = static_cast<std::uint16_t>(t % width);
                    y = static_cast<std::uint16_t>(cy);
                    break;
                }
                case Mode::Curve: {
                    // Sinusoidal vertical motion.
                    const double phase = static_cast<double>(t) * 0.01;
                    x = static_cast<std::uint16_t>(t % width);
                    y = static_cast<std::uint16_t>(
                        cy + static_cast<int>(radius * 0.5 * std::sin(phase)));
                    break;
                }
                case Mode::Rotation: {
                    // Circular motion about the centre.
                    const double ang = static_cast<double>(t) * 0.01;
                    x = static_cast<std::uint16_t>(
                        cx + static_cast<int>(radius * 0.5 * std::cos(ang)));
                    y = static_cast<std::uint16_t>(
                        cy + static_cast<int>(radius * 0.5 * std::sin(ang)));
                    break;
                }
            }
            events_.emplace_back(x, y, p, t, true);
        }
        return events_.size();
    }

    /// @brief Injects Poisson-distributed background activity noise.
    /// @param rate_hz  Target noise rate per pixel in Hz.
    /// @param width    Sensor width in pixels.
    /// @param height   Sensor height in pixels.
    /// @return Number of noise events injected (is_signal=false).
    std::size_t inject_poisson_noise(float rate_hz, int width, int height) {
        if (rate_hz <= 0.0f || width <= 0 || height <= 0) {
            return 0;
        }
        std::poisson_distribution<int> dist(
            static_cast<double>(rate_hz) * static_cast<double>(width * height) *
            1e-6);  // expected count per microsecond
        const Metavision::timestamp duration_us =
            events_.empty() ? 100000
                            : events_.back().t + 1;
        std::size_t injected = 0;
        for (Metavision::timestamp t = 0; t < duration_us; ++t) {
            int n = dist(rng_);
            for (int i = 0; i < n; ++i) {
                std::uint16_t x = static_cast<std::uint16_t>(
                    width_dist_(rng_, width));
                std::uint16_t y = static_cast<std::uint16_t>(
                    height_dist_(rng_, height));
                short p = static_cast<short>(polarity_dist_(rng_));
                events_.emplace_back(x, y, p, t, false);
                ++injected;
            }
        }
        return injected;
    }

    /// @brief Randomly removes a fraction of signal events (dropout noise).
    /// @param drop_rate Fraction of signal events to remove in [0, 1].
    /// @return Number of signal events removed.
    std::size_t inject_dropout(float drop_rate) {
        if (drop_rate <= 0.0f) {
            return 0;
        }
        if (drop_rate >= 1.0f) {
            std::size_t removed = 0;
            for (const auto& e : events_) {
                if (e.is_signal) { ++removed; }
            }
            events_.clear();
            return removed;
        }
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        std::size_t removed = 0;
        std::vector<SignalNoiseEvent> kept;
        kept.reserve(events_.size());
        for (auto& e : events_) {
            if (e.is_signal && u(rng_) < drop_rate) {
                ++removed;
            } else {
                kept.push_back(e);
            }
        }
        events_ = std::move(kept);
        return removed;
    }

    /// @brief Runs the candidate filter over the event stream, recording which
    /// events pass. The filter must expose `bool process(const Event&)`.
    /// @param filter Configured NoiseFilter instance under test.
    /// @return Number of events accepted by the filter.
    template <class FilterT>
    std::size_t run_filter(FilterT& filter) {
        passed_.assign(events_.size(), false);
        std::size_t accepted = 0;
        for (std::size_t i = 0; i < events_.size(); ++i) {
            const Event& e = events_[i];
            bool ok = filter.process(e);
            passed_[i] = ok;
            if (ok) { ++accepted; }
        }
        return accepted;
    }

    /// @brief Computes confusion-matrix metrics from the last run_filter() call.
    /// Must be called after run_filter().
    Metrics compute_metrics() const {
        Metrics m;
        for (std::size_t i = 0; i < events_.size(); ++i) {
            const bool signal = events_[i].is_signal;
            const bool accepted = (i < passed_.size()) ? passed_[i] : false;
            if (signal && accepted) { ++m.tp; }
            else if (!signal && accepted) { ++m.fp; }
            else if (!signal && !accepted) { ++m.tn; }
            else { ++m.fn; }  // signal && !accepted
        }
        const int pred_pos = m.tp + m.fp;
        const int pred_sig = m.tp + m.fn;
        m.precision = (pred_pos > 0)
            ? static_cast<float>(m.tp) / static_cast<float>(pred_pos)
            : 0.0f;
        m.recall = (pred_sig > 0)
            ? static_cast<float>(m.tp) / static_cast<float>(pred_sig)
            : 0.0f;
        const float pr = m.precision + m.recall;
        m.f1 = (pr > 0.0f)
            ? 2.0f * m.precision * m.recall / pr
            : 0.0f;
        return m;
    }

    /// @brief Read-only access to the current annotated event stream.
    const std::vector<SignalNoiseEvent>& events() const { return events_; }
    /// @brief Number of events currently in the stream.
    std::size_t size() const { return events_.size(); }
    /// @brief Clears the event stream and recorded pass/fail flags.
    void clear() {
        events_.clear();
        passed_.clear();
    }

private:
    // Stateless helper for uniform integer sampling in [0, n).
    struct UniformInt {
        int operator()(std::mt19937& g, int n) const {
            if (n <= 0) { return 0; }
            std::uniform_int_distribution<int> local(0, n - 1);
            return local(g);
        }
    };

    Mode mode_{Mode::Line};
    std::vector<SignalNoiseEvent> events_;
    std::vector<char> passed_;  // 1 = accepted by filter, 0 = filtered out
    std::mt19937 rng_;
    UniformInt width_dist_{};
    UniformInt height_dist_{};
    std::bernoulli_distribution polarity_dist_{0.5};
};

} // namespace gui_algo

#endif // GUI_ALGO_TESTS_NOISE_TESTER_H
