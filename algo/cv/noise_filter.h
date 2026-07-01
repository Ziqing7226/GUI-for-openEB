// algo/cv/noise_filter.h — multi-mode event noise filter.
//
// Self-developed (design §4.3.5), inspired by the jAER filter suite
// (net/sf/jaer/eventprocessing/filter/). Provides 8 denoising modes:
//   BAF          — Background Activity Filter (Delbruck 2008): 3x3 neighbour
//                  correlation within dt_us.
//   STCF         — SpatioTemporal Correlation Filter (Guo & Delbruck 2022):
//                  >= min_neighbors correlated neighbours in 3x3 within
//                  correlation_time_s, optional polarity match / coincidence.
//   Refractory   — per-pixel refractory period (ISI threshold).
//   DWF          — Double-Window Filter: signal vs noise ISI bands.
//   AgePolarity  — soft age score (1 - dt/tau) weighted by polarity match.
//   Harmonic     — notch on per-pixel ISI at the mains line frequency.
//   Repetitious  — drops events recurring at a fixed period (screen flicker).
//   SpatialBP    — center/surround time-surface differencing (small-target
//                  enhancement).
// Two common options link to the rest of the pipeline: filter_hot_pixels
// (internal n_sigma hot-pixel suppression, see §4.3.6) and
// adaptive_correlation_time (scales the active time threshold by the local
// event rate, jAER NoiseFilterControl). Uses a 2D most-recent-timestamp map
// (per-polarity + any-polarity) for spatial correlation. Header-only.

#ifndef GUI_ALGO_CV_NOISE_FILTER_H
#define GUI_ALGO_CV_NOISE_FILTER_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <metavision/sdk/base/utils/timestamp.h>

#include "algo/common/event.h"
#include "algo/common/event_packet.h"

namespace gui_algo {

/// @brief Multi-mode event denoiser operating on a streaming event packet.
class NoiseFilter {
public:
    enum class Mode {
        BAF,            ///< Background activity filter
        STCF,           ///< Spatio-temporal correlation filter
        Refractory,     ///< Per-pixel refractory period
        DWF,            ///< Double-window filter
        AgePolarity,    ///< Age + polarity soft score
        Harmonic,       ///< Mains-frequency harmonic notch
        Repetitious,    ///< Periodic repetition filter
        SpatialBP,      ///< Spatial band-pass (center/surround)
    };

    enum class LineFreq { Hz50, Hz60 };

    NoiseFilter(int width, int height, Mode mode = Mode::STCF)
        : width_(width), height_(height), mode_(mode),
          last_any_(static_cast<std::size_t>(width) * height, 0),
          hp_counts_(static_cast<std::size_t>(width) * height, 0) {
        last_pol_[0].assign(static_cast<std::size_t>(width) * height, 0);
        last_pol_[1].assign(static_cast<std::size_t>(width) * height, 0);
    }

    // Mode + common options ------------------------------------------------
    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    void set_filter_hot_pixels(bool v) { filter_hot_pixels_ = v; }
    void set_adaptive_correlation_time(bool v) { adaptive_correlation_time_ = v; }
    bool filter_hot_pixels() const { return filter_hot_pixels_; }
    bool adaptive_correlation_time() const { return adaptive_correlation_time_; }

    // BAF ------------------------------------------------------------------
    void set_baf_dt_us(int v) { baf_dt_us_ = clamp_i(v, 1000, 100000); }
    void set_baf_subsample_by(int v) { baf_subsample_by_ = clamp_i(v, 0, 4); }
    int baf_dt_us() const { return baf_dt_us_; }
    int baf_subsample_by() const { return baf_subsample_by_; }

    // STCF -----------------------------------------------------------------
    void set_correlation_time_s(double v) { stcf_corr_s_ = clamp_d(v, 0.001, 0.1); }
    void set_min_neighbors(int v) { stcf_min_neighbors_ = clamp_i(v, 1, 8); }
    void set_require_polarity_match(bool v) { stcf_require_polarity_match_ = v; }
    void set_allow_coincidence(bool v) { stcf_allow_coincidence_ = v; }
    double correlation_time_s() const { return stcf_corr_s_; }
    int min_neighbors() const { return stcf_min_neighbors_; }
    bool require_polarity_match() const { return stcf_require_polarity_match_; }
    bool allow_coincidence() const { return stcf_allow_coincidence_; }

    // Refractory -----------------------------------------------------------
    void set_refractory_period_us(int v) { refractory_period_us_ = clamp_i(v, 100, 100000); }
    int refractory_period_us() const { return refractory_period_us_; }

    // DWF ------------------------------------------------------------------
    void set_signal_window_us(int v) { dwf_signal_us_ = clamp_i(v, 1000, 50000); }
    void set_noise_window_us(int v) { dwf_noise_us_ = clamp_i(v, 10000, 500000); }
    int signal_window_us() const { return dwf_signal_us_; }
    int noise_window_us() const { return dwf_noise_us_; }

    // AgePolarity ----------------------------------------------------------
    void set_tau_us(int v) { agep_tau_us_ = clamp_i(v, 1000, 100000); }
    void set_age_threshold(double v) { agep_threshold_ = clamp_d(v, 0.0, 1.0); }
    int tau_us() const { return agep_tau_us_; }
    double age_threshold() const { return agep_threshold_; }

    // Harmonic -------------------------------------------------------------
    void set_line_freq(LineFreq f) { harm_line_freq_hz_ = (f == LineFreq::Hz50) ? 50 : 60; }
    void set_notch_q(double v) { harm_notch_q_ = clamp_d(v, 1.0, 50.0); }
    int line_freq_hz() const { return harm_line_freq_hz_; }
    double notch_q() const { return harm_notch_q_; }

    // Repetitious ----------------------------------------------------------
    void set_period_us(int v) { rep_period_us_ = clamp_i(v, 1000, 1000000); }
    void set_tolerance_us(int v) { rep_tolerance_us_ = clamp_i(v, 100, 10000); }
    int period_us() const { return rep_period_us_; }
    int tolerance_us() const { return rep_tolerance_us_; }

    // SpatialBP ------------------------------------------------------------
    void set_center_radius_px(int v) { sbp_center_ = clamp_i(v, 1, 10); }
    void set_surround_radius_px(int v) { sbp_surround_ = clamp_i(v, 5, 30); }
    int center_radius_px() const { return sbp_center_; }
    int surround_radius_px() const { return sbp_surround_; }

    int width() const { return width_; }
    int height() const { return height_; }

    /// @brief Processes an event packet, updating internal correlation maps.
    /// @return Number of events that PASSED the filter (the filtered stream).
    std::size_t process(EventPacket& events) {
        std::size_t kept = 0;
        std::size_t total = 0;
        for (const auto& e : events) {
            if (decide_and_update(e)) ++kept;
            ++total;
        }
        last_total_ = total;
        last_kept_ = kept;
        return kept;
    }

    /// @brief Filters a mutable packet in place (compaction). Returns the new
    ///        (kept) event count.
    std::size_t filter(MutableEventPacket& events) {
        std::size_t out = 0;
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (decide_and_update(events[i])) {
                events[out] = events[i];
                ++out;
            }
        }
        last_total_ = events.size();
        last_kept_ = out;
        return out;
    }

    /// @brief Per-event decision for a raw pointer buffer; returns the kept
    ///        count and compacts in place.
    std::size_t filter(Event* events, std::size_t count) {
        std::size_t out = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (decide_and_update(events[i])) {
                events[out] = events[i];
                ++out;
            }
        }
        last_total_ = count;
        last_kept_ = out;
        return out;
    }

    std::size_t last_total() const { return last_total_; }
    std::size_t last_kept() const { return last_kept_; }
    std::size_t last_filtered() const {
        return last_total_ >= last_kept_ ? last_total_ - last_kept_ : 0;
    }
    /// @brief Fraction of events removed in the last process/filter call [0,1].
    double filter_rate() const {
        return last_total_ > 0
            ? static_cast<double>(last_filtered()) / static_cast<double>(last_total_)
            : 0.0;
    }

    void reset() {
        std::fill(last_any_.begin(), last_any_.end(), 0);
        std::fill(last_pol_[0].begin(), last_pol_[0].end(), 0);
        std::fill(last_pol_[1].begin(), last_pol_[1].end(), 0);
        std::fill(hp_counts_.begin(), hp_counts_.end(), 0);
        hp_start_ = 0;
        hp_thresh_ = 0.0;
        rate_start_ = 0;
        rate_events_ = 0;
        rate_eps_ = 0.0;
        scale_ = 1.0;
        last_total_ = 0;
        last_kept_ = 0;
    }

private:
    static int clamp_i(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double clamp_d(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    std::size_t idx_of(int x, int y) const {
        return static_cast<std::size_t>(y) * width_ + x;
    }

    /// @brief Applies the adaptive-correlation-time scaling to a base threshold.
    Metavision::timestamp thr(Metavision::timestamp base_us) const {
        if (!adaptive_correlation_time_ || scale_ <= 0.0) return base_us;
        const double v = static_cast<double>(base_us) * scale_;
        if (v < 1.0) return 1;
        return static_cast<Metavision::timestamp>(v);
    }

    /// @brief Updates the event-rate estimate and the adaptive scale factor.
    void update_rate(Metavision::timestamp t) {
        ++rate_events_;
        if (rate_start_ == 0) rate_start_ = t;
        const Metavision::timestamp span = t - rate_start_;
        if (span >= rate_window_us_) {
            const double eps = static_cast<double>(rate_events_) /
                               static_cast<double>(span); // events/us
            rate_eps_ = eps;
            const double ref = 0.05; // ~50 kEvents/s reference rate
            double s = (eps > 0.0) ? (ref / eps) : 1.0;
            if (s < 0.25) s = 0.25;
            if (s > 4.0) s = 4.0;
            scale_ = s;
            rate_start_ = t;
            rate_events_ = 0;
        }
    }

    /// @brief Core per-event decision; updates timestamp surfaces.
    bool decide_and_update(const Event& e) {
        update_rate(e.t);
        if (e.x >= width_ || e.y >= height_) return true;
        bool pass = mode_pass(e);
        if (pass && filter_hot_pixels_ && !hot_ok(e)) pass = false;
        const std::size_t idx = idx_of(e.x, e.y);
        last_any_[idx] = e.t;
        last_pol_[e.p ? 1 : 0][idx] = e.t;
        return pass;
    }

    bool mode_pass(const Event& e) const {
        switch (mode_) {
            case Mode::BAF:         return baf_pass(e);
            case Mode::STCF:        return stcf_pass(e);
            case Mode::Refractory:  return refractory_pass(e);
            case Mode::DWF:         return dwf_pass(e);
            case Mode::AgePolarity: return age_polarity_pass(e);
            case Mode::Harmonic:    return harmonic_pass(e);
            case Mode::Repetitious: return repetitious_pass(e);
            case Mode::SpatialBP:   return spatialbp_pass(e);
        }
        return true; // unreachable
    }

    bool baf_pass(const Event& e) const {
        const Metavision::timestamp dt = thr(baf_dt_us_);
        const int step = 1 << baf_subsample_by_;
        for (int dy = -1; dy <= 1; dy += step) {
            const int ny = e.y + dy;
            if (ny < 0 || ny >= height_) continue;
            for (int dx = -1; dx <= 1; dx += step) {
                if (dx == 0 && dy == 0) continue;
                const int nx = e.x + dx;
                if (nx < 0 || nx >= width_) continue;
                const Metavision::timestamp lt = last_any_[idx_of(nx, ny)];
                if (lt == 0) continue;
                const Metavision::timestamp diff = e.t - lt;
                if (diff >= 0 && diff <= dt) return true;
            }
        }
        return false;
    }

    bool stcf_pass(const Event& e) const {
        const Metavision::timestamp dt =
            thr(static_cast<Metavision::timestamp>(stcf_corr_s_ * 1e6));
        const int pol = e.p ? 1 : 0;
        int count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            const int ny = e.y + dy;
            if (ny < 0 || ny >= height_) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = e.x + dx;
                if (nx < 0 || nx >= width_) continue;
                const Metavision::timestamp lt = stcf_require_polarity_match_
                    ? last_pol_[pol][idx_of(nx, ny)]
                    : last_any_[idx_of(nx, ny)];
                if (lt == 0) continue;
                const Metavision::timestamp diff = e.t - lt;
                if (diff < 0 || diff > dt) continue;
                if (diff == 0 && !stcf_allow_coincidence_) continue; // coincidence
                ++count;
            }
        }
        return count >= stcf_min_neighbors_;
    }

    bool refractory_pass(const Event& e) const {
        const Metavision::timestamp lt = last_any_[idx_of(e.x, e.y)];
        if (lt == 0 || e.t < lt) return true;
        return (e.t - lt) >= thr(refractory_period_us_);
    }

    bool dwf_pass(const Event& e) const {
        const Metavision::timestamp lt = last_any_[idx_of(e.x, e.y)];
        if (lt == 0 || e.t < lt) return true;
        const Metavision::timestamp isi = e.t - lt;
        if (isi <= thr(dwf_signal_us_)) return true;   // signal band
        if (isi <= thr(dwf_noise_us_)) return false;   // noise band
        return true;                                   // long silence
    }

    bool age_polarity_pass(const Event& e) const {
        const Metavision::timestamp lt_any = last_any_[idx_of(e.x, e.y)];
        if (lt_any == 0 || e.t < lt_any) return true;
        const double tau = static_cast<double>(thr(agep_tau_us_));
        const double age = 1.0 - static_cast<double>(e.t - lt_any) / tau;
        if (age <= 0.0) return false;
        const Metavision::timestamp lt_pol =
            last_pol_[e.p ? 1 : 0][idx_of(e.x, e.y)];
        const double weight = (lt_pol == lt_any) ? 1.0 : 0.5;
        return (age * weight) >= agep_threshold_;
    }

    bool harmonic_pass(const Event& e) const {
        const Metavision::timestamp lt = last_any_[idx_of(e.x, e.y)];
        if (lt == 0 || e.t < lt) return true;
        const double period = 1e6 / static_cast<double>(harm_line_freq_hz_);
        const double tol = period / (2.0 * harm_notch_q_);
        const double isi = static_cast<double>(e.t - lt);
        const double m = std::fmod(isi, period);
        const double d = (m < tol) ? m : (period - m);
        return d > tol; // pass unless ISI is near a line-period multiple
    }

    bool repetitious_pass(const Event& e) const {
        const Metavision::timestamp lt = last_any_[idx_of(e.x, e.y)];
        if (lt == 0 || e.t < lt) return true;
        const double isi = static_cast<double>(e.t - lt);
        const double period = static_cast<double>(rep_period_us_);
        const double tol = static_cast<double>(rep_tolerance_us_);
        if (isi < period - tol) return true;           // too fast -> not periodic
        const double m = std::fmod(isi, period);
        const double d = (m < tol) ? m : (period - m);
        return d > tol; // pass unless ISI is near a period multiple
    }

    bool spatialbp_pass(const Event& e) const {
        const Metavision::timestamp window =
            thr(static_cast<Metavision::timestamp>(sbp_surround_) * 1000);
        const int cr = sbp_center_;
        const int sr = sbp_surround_;
        const int cr2 = cr * cr;
        const int sr2 = sr * sr;
        double center = 0.0, surround = 0.0;
        for (int dy = -sr; dy <= sr; ++dy) {
            const int ny = e.y + dy;
            if (ny < 0 || ny >= height_) continue;
            for (int dx = -sr; dx <= sr; ++dx) {
                const int nx = e.x + dx;
                if (nx < 0 || nx >= width_) continue;
                const int r2 = dx * dx + dy * dy;
                if (r2 > sr2) continue;
                const Metavision::timestamp lt = last_any_[idx_of(nx, ny)];
                if (lt == 0) continue;
                const Metavision::timestamp diff = e.t - lt;
                if (diff < 0 || diff > window) continue;
                const double val =
                    1.0 - static_cast<double>(diff) / static_cast<double>(window);
                if (r2 <= cr2) center += val; else surround += val;
            }
        }
        return center > surround; // small-target (center) enhancement
    }

    // Internal hot-pixel suppression (links to §4.3.6) --------------------
    bool hot_ok(const Event& e) {
        const std::size_t idx = idx_of(e.x, e.y);
        ++hp_counts_[idx];
        if (hp_start_ == 0) hp_start_ = e.t;
        if (e.t - hp_start_ >= hp_window_us_) {
            recompute_hp_threshold();
            hp_start_ = e.t;
        }
        return static_cast<double>(hp_counts_[idx]) <= hp_thresh_;
    }

    void recompute_hp_threshold() {
        const double n = static_cast<double>(hp_counts_.size());
        double sum = 0.0;
        for (auto c : hp_counts_) sum += static_cast<double>(c);
        const double mean = n > 0.0 ? sum / n : 0.0;
        double var = 0.0;
        if (n > 0.0) {
            for (auto c : hp_counts_) {
                const double d = static_cast<double>(c) - mean;
                var += d * d;
            }
            var /= n;
        }
        hp_thresh_ = mean + 4.0 * std::sqrt(var);
        std::fill(hp_counts_.begin(), hp_counts_.end(), 0);
    }

    int width_;
    int height_;
    Mode mode_;

    // Mode parameters (defaults per design §4.3.5) ------------------------
    Metavision::timestamp baf_dt_us_{25000};
    int baf_subsample_by_{0};
    double stcf_corr_s_{0.025};
    int stcf_min_neighbors_{2};
    bool stcf_require_polarity_match_{true};
    bool stcf_allow_coincidence_{false};
    Metavision::timestamp refractory_period_us_{1000};
    Metavision::timestamp dwf_signal_us_{5000};
    Metavision::timestamp dwf_noise_us_{50000};
    Metavision::timestamp agep_tau_us_{10000};
    double agep_threshold_{0.5};
    int harm_line_freq_hz_{50};
    double harm_notch_q_{10.0};
    Metavision::timestamp rep_period_us_{20000};
    Metavision::timestamp rep_tolerance_us_{1000};
    int sbp_center_{2};
    int sbp_surround_{10};

    bool filter_hot_pixels_{false};
    bool adaptive_correlation_time_{false};

    // Timestamp surfaces ---------------------------------------------------
    std::vector<Metavision::timestamp> last_any_;
    std::array<std::vector<Metavision::timestamp>, 2> last_pol_;

    // Hot-pixel bookkeeping ------------------------------------------------
    std::vector<std::uint32_t> hp_counts_;
    Metavision::timestamp hp_start_{0};
    double hp_thresh_{0.0};
    static constexpr Metavision::timestamp hp_window_us_{1000000};

    // Adaptive correlation-time bookkeeping --------------------------------
    Metavision::timestamp rate_start_{0};
    std::uint64_t rate_events_{0};
    double rate_eps_{0.0};
    double scale_{1.0};
    static constexpr Metavision::timestamp rate_window_us_{50000};

    // Last-call statistics -------------------------------------------------
    std::size_t last_total_{0};
    std::size_t last_kept_{0};
};

} // namespace gui_algo

#endif // GUI_ALGO_CV_NOISE_FILTER_H
