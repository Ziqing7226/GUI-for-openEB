// algo/common/particle_filter.h — Monte Carlo particle filter for 2D tracking.
//
// Inspired by jAER ParticleFilter. Maintains a weighted set of particles
// representing the posterior distribution of a target's 2D position. The
// standard predict → weight → resample cycle is used. Motion model is
// constant-velocity with Gaussian diffusion; the likelihood is a Gaussian over
// distance to the nearest event (or measurement). Header-only.

#ifndef GUI_ALGO_COMMON_PARTICLE_FILTER_H
#define GUI_ALGO_COMMON_PARTICLE_FILTER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "kmeans.h"  // Point2D

namespace gui_algo {

/// @brief 2D particle filter for single-target tracking.
class ParticleFilter {
public:
    struct Particle {
        double x, y, vx, vy, weight;
    };

    /// @brief Constructs the filter.
    /// @param n_particles Number of particles.
    /// @param process_noise_std Per-step position diffusion std-dev (px).
    /// @param measurement_noise_std Measurement likelihood std-dev (px).
    /// @param dt Time step (seconds).
    /// @param seed RNG seed.
    ParticleFilter(std::size_t n_particles = 200,
                   double process_noise_std = 3.0,
                   double measurement_noise_std = 5.0,
                   double dt = 0.033,
                   std::uint64_t seed = 42)
        : n_(n_particles), q_std_(process_noise_std),
          r_std_(measurement_noise_std), dt_(dt), rng_(seed) {
        particles_.resize(n_);
    }

    /// @brief Initialises all particles around a seed position with zero velocity.
    void init(double x, double y) {
        std::normal_distribution<double> noise(0.0, q_std_);
        for (auto& p : particles_) {
            p.x = x + noise(rng_);
            p.y = y + noise(rng_);
            p.vx = 0.0;
            p.vy = 0.0;
            p.weight = 1.0 / static_cast<double>(n_);
        }
    }

    /// @brief Predict step: advance particles by constant-velocity + diffusion.
    void predict() {
        std::normal_distribution<double> noise(0.0, q_std_);
        for (auto& p : particles_) {
            p.x += p.vx * dt_ + noise(rng_);
            p.y += p.vy * dt_ + noise(rng_);
        }
    }

    /// @brief Update particle weights using a position measurement.
    void update(double mx, double my) {
        const double two_r2 = 2.0 * r_std_ * r_std_;
        double sum = 0.0;
        for (auto& p : particles_) {
            const double dx = p.x - mx, dy = p.y - my;
            const double d2 = dx * dx + dy * dy;
            p.weight = std::exp(-d2 / two_r2);
            sum += p.weight;
        }
        if (sum <= 0.0) {
            const double w = 1.0 / static_cast<double>(n_);
            for (auto& p : particles_) p.weight = w;
        } else {
            const double inv = 1.0 / sum;
            for (auto& p : particles_) p.weight *= inv;
        }
    }

    /// @brief Low-variance systematic resampling. Call after update().
    void resample() {
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        const double r = u01(rng_) / static_cast<double>(n_);
        double c = particles_[0].weight;
        std::vector<Particle> next(n_);
        std::size_t i = 0;
        for (std::size_t m = 0; m < n_; ++m) {
            double u = r + static_cast<double>(m) / static_cast<double>(n_);
            while (u > c && i < n_ - 1) {
                ++i;
                c += particles_[i].weight;
            }
            next[m] = particles_[i];
            next[m].weight = 1.0 / static_cast<double>(n_);
        }
        particles_.swap(next);
    }

    /// @brief Weighted mean position estimate.
    Point2D estimate() const {
        double x = 0.0, y = 0.0;
        for (const auto& p : particles_) {
            x += p.x * p.weight;
            y += p.y * p.weight;
        }
        return Point2D(x, y);
    }

    const std::vector<Particle>& particles() const { return particles_; }
    std::size_t num_particles() const { return n_; }

private:
    std::size_t n_;
    double q_std_;
    double r_std_;
    double dt_;
    std::mt19937_64 rng_;
    std::vector<Particle> particles_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_PARTICLE_FILTER_H
