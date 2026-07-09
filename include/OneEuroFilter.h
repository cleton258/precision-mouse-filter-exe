#pragma once
// OneEuroFilter.h
//
// Speed-adaptive low-pass filter, adapted from the 1-Euro Filter algorithm
// (Casiez, Roussel & Vogel, "1-Euro Filter: A Simple Speed-based Low-pass
// Filter for Noisy Input in Interactive Systems", CHI 2012).
//
// The original algorithm filters a POSITION signal and derives velocity by
// differencing consecutive samples. Raw mouse input already arrives as a
// DELTA (movement-since-last-report), i.e. the signal is already in the
// algorithm's "derivative" domain. AdaptiveDeltaFilter below is adapted to
// filter that delta stream directly, which is simpler and avoids an
// unnecessary accumulate/re-differentiate round trip.
//
// Key property preserved from the original: the filter is a convex blend
//     y[i] = alpha * x[i] + (1 - alpha) * y[i-1],   alpha in (0, 1]
// Its steady-state (DC) gain is exactly 1.0 for ANY alpha -- adapting alpha
// only changes how much temporal lag/averaging is applied, never the
// magnitude/gain of the signal. This is what lets the filter remove jitter
// without introducing acceleration (positive or negative): gain is controlled
// exclusively and separately by the user's flat sensitivity multiplier.
//
// At low speed, alpha is small (heavy smoothing -> jitter removed).
// At high speed, alpha approaches 1 (near pass-through -> no added lag).

#include <cmath>
#include <algorithm>

namespace pmf {

constexpr double kPi = 3.14159265358979323846;

// Single-pole exponential low-pass filter.
class LowPassFilter {
public:
    double Filter(double x, double alpha) {
        double result = initialized_ ? (alpha * x + (1.0 - alpha) * prev_) : x;
        prev_ = result;
        initialized_ = true;
        return result;
    }

    void Reset() {
        initialized_ = false;
        prev_ = 0.0;
    }

private:
    bool initialized_ = false;
    double prev_ = 0.0;
};

// Filters a single axis of raw movement-per-report data with a cutoff
// frequency that rises with the (smoothed) speed of that axis.
class AdaptiveDeltaFilter {
public:
    // minCutoff: cutoff frequency (Hz) used at (near) zero speed. Lower value
    //            => stronger smoothing at rest / slow movement.
    // beta:      how fast the cutoff rises with speed. Higher value => the
    //            filter opens up (stops smoothing) sooner as speed increases.
    void SetParameters(double minCutoff, double beta) {
        minCutoff_ = minCutoff;
        beta_ = beta;
    }

    // delta: raw movement since the previous report (device counts).
    // dt:    seconds since the previous report (must be > 0; caller should
    //        clamp pathological values before calling).
    double Filter(double delta, double dt) {
        dt = std::max(dt, kMinDt);

        double instantaneousSpeed = std::fabs(delta) / dt; // counts / second
        double speed = speedFilter_.Filter(instantaneousSpeed, Alpha(dt, kDerivativeCutoffHz));

        double cutoff = minCutoff_ + beta_ * speed;
        return valueFilter_.Filter(delta, Alpha(dt, cutoff));
    }

    void Reset() {
        speedFilter_.Reset();
        valueFilter_.Reset();
    }

private:
    static double Alpha(double dt, double cutoffHz) {
        double tau = 1.0 / (2.0 * kPi * cutoffHz);
        return 1.0 / (1.0 + tau / dt);
    }

    double minCutoff_ = 1.0;
    double beta_ = 0.0;
    LowPassFilter speedFilter_;
    LowPassFilter valueFilter_;

    static constexpr double kDerivativeCutoffHz = 1.0; // smooths the speed estimate itself
    static constexpr double kMinDt = 1e-4;             // 0.1 ms floor, guards div-by-zero
};

} // namespace pmf
