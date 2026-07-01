// algo/common/periodic_spline.h — periodic cubic spline for trajectory smoothing.
//
// Inspired by jAER PeriodicSpline. Fits a periodic cubic spline to a closed
// trajectory (e.g. a circle/ellipse traced by a rotating target) and resamples
// it at uniform parameter values for smoothing / interpolation. Uses the
// tridiagonal solve for periodic boundary conditions. Header-only.

#ifndef GUI_ALGO_COMMON_PERIODIC_SPLINE_H
#define GUI_ALGO_COMMON_PERIODIC_SPLINE_H

#include <cmath>
#include <cstddef>
#include <vector>

#include "kmeans.h"  // Point2D

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gui_algo {

/// @brief Periodic cubic spline for closed-curve trajectory smoothing.
class PeriodicSpline {
public:
    /// @brief Fits the spline to @p points (treated as a closed polygon in order).
    /// @param xs,ys Coordinate arrays (must be equal length >= 3).
    void fit(const std::vector<double>& xs, const std::vector<double>& ys) {
        const std::size_t n = xs.size();
        if (n != ys.size() || n < 3) return;
        xs_ = xs;
        ys_ = ys;
        // Periodic parameter t in [0, 2π); uniform spacing.
        ts_.resize(n);
        const double step = 2.0 * M_PI / static_cast<double>(n);
        for (std::size_t i = 0; i < n; ++i) ts_[i] = static_cast<double>(i) * step;
        solve_periodic(xs_, cx_);
        solve_periodic(ys_, cy_);
    }

    /// @brief Evaluates the smoothed spline at parameter @p t (any real; wraps).
    Point2D evaluate(double t) const {
        const std::size_t n = ts_.size();
        if (n == 0) return Point2D();
        // Wrap t into [0, 2π).
        const double two_pi = 2.0 * M_PI;
        t = t - two_pi * std::floor(t / two_pi);
        // Find segment [i, i+1).
        std::size_t i = static_cast<std::size_t>(t / (two_pi / static_cast<double>(n)));
        if (i >= n - 1) i = n - 2;
        const double t0 = ts_[i], t1 = ts_[i + 1];
        const double dt = t1 - t0;
        const double u = (t - t0) / dt;
        const double u2 = u * u, u3 = u2 * u;
        const double h00 = 2 * u3 - 3 * u2 + 1;
        const double h10 = u3 - 2 * u2 + u;
        const double h01 = -2 * u3 + 3 * u2;
        const double h11 = u3 - u2;
        const double x = h00 * xs_[i] + h10 * dt * cx_[i] +
                         h01 * xs_[(i + 1) % n] + h11 * dt * cx_[(i + 1) % n];
        const double y = h00 * ys_[i] + h10 * dt * cy_[i] +
                         h01 * ys_[(i + 1) % n] + h11 * dt * cy_[(i + 1) % n];
        return Point2D(x, y);
    }

    /// @brief Resamples the curve into @p m uniformly-spaced points.
    std::vector<Point2D> resample(std::size_t m) const {
        std::vector<Point2D> out;
        out.reserve(m);
        const double two_pi = 2.0 * M_PI;
        for (std::size_t i = 0; i < m; ++i) {
            out.push_back(evaluate(static_cast<double>(i) / m * two_pi));
        }
        return out;
    }

private:
    /// @brief Solves the periodic tridiagonal system for second derivatives.
    /// Given values y[0..n-1] with uniform spacing h, computes m[0..n-1] such
    /// that the spline is C2-continuous and periodic (y[0]==y[n]).
    void solve_periodic(const std::vector<double>& y, std::vector<double>& m) {
        const std::size_t n = y.size();
        if (n < 3) { m.assign(n, 0.0); return; }
        // Standard periodic tridiagonal solve (Thomas algorithm variant).
        std::vector<double> a(n - 1, 1.0), b(n - 1, 4.0), c(n - 1, 1.0), d(n - 1);
        for (std::size_t i = 0; i < n - 1; ++i) {
            d[i] = 3.0 * (y[(i + 2) % n] - y[i]);
        }
        // Forward sweep.
        std::vector<double> cp(n - 1), dp(n - 1);
        cp[0] = c[0] / b[0];
        dp[0] = d[0] / b[0];
        for (std::size_t i = 1; i < n - 1; ++i) {
            const double denom = b[i] - a[i] * cp[i - 1];
            cp[i] = c[i] / denom;
            dp[i] = (d[i] - a[i] * dp[i - 1]) / denom;
        }
        // Back substitution.
        m.resize(n);
        m[n - 2] = dp[n - 2];
        for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(n) - 3; i >= 0; --i) {
            m[i] = dp[i] - cp[i] * m[i + 1];
        }
        m[n - 1] = m[0];
    }

    std::vector<double> ts_, xs_, ys_, cx_, cy_;
};

} // namespace gui_algo

#endif // GUI_ALGO_COMMON_PERIODIC_SPLINE_H
