#pragma once
#include "OneEuroFilter.h"

namespace pmf {

// Shape used by the final speed->gain response curve (req 12). "Custom"
// reuses customCurveExponent as its shaping parameter so a future UI can
// expose a single numeric field instead of a full curve editor.
enum class ResponseCurve {
    Linear = 0,
    Exponential = 1,
    Smooth = 2,
    Custom = 3
};

// All user-facing sliders live in [0, 100] except: sensitivity (a flat
// multiplier), mouseDpi (used only to normalize speed calculations across
// different sensor DPI settings), accelerationControl ([-100, 100]),
// customCurveExponent (a shaping exponent), and responseCurve (an enum).
//
// Every new (Phase 1) field below defaults to a value that reproduces the
// original pipeline's output exactly (0 = feature inactive, Linear curve at
// 0 intensity = neutral gain). Existing profiles/configs that predate these
// fields load with these defaults and behave exactly as before.
struct FilterSettings {
    // --- requirement 1: Suavizacao Principal (existing) ---
    double filterIntensity = 50.0;       // jitter removal at rest
    double smoothingIntensity = 50.0;    // how long smoothing persists into motion
    double sensitivity = 1.0;            // flat multiplier only -- never speed-dependent
    double straightLineIntensity = 40.0; // extra damping of the minor axis

    // --- requirement 3: Anti-Spike (existing) ---
    double antiSpikeSensitivity = 50.0;  // how aggressively true outliers are clamped

    // --- requirement 2: Anti-Jitter (dedicated deadband, independent of the
    //     speed-adaptive filter above) ---
    double antiJitterIntensity = 0.0;

    // --- requirement 4: Snap Angle (optional, rotates near-straight moves
    //     toward the nearest 45-degree compass direction) ---
    double snapAngleIntensity = 0.0;

    // --- requirement 5: extra stabilization for high-DPI sensors, on top of
    //     the DPI normalization already applied to beta ---
    double highSensitivityFilterIntensity = 0.0;

    // --- requirement 6: Flick Stabilizer (settles overshoot/ringing right
    //     after a fast flick) ---
    double flickStabilizerIntensity = 0.0;

    // --- requirements 7/8: independent per-axis stabilizers ---
    double horizontalStabilizerIntensity = 0.0;
    double verticalStabilizerIntensity = 0.0;

    // --- requirement 9: Motion Prediction (extrapolates the user's own
    //     already-in-progress motion very slightly; never creates movement
    //     that wasn't already happening) ---
    double motionPredictionIntensity = 0.0;

    // --- requirement 10: Adaptive Noise Reduction (widens smoothing
    //     automatically when recent input looks unstable/noisy) ---
    double adaptiveNoiseReduction = 0.0;

    // --- requirement 11: Acceleration Control (-100 = strongest
    //     deceleration of fast motion, +100 = strongest amplification) ---
    double accelerationControl = 0.0;

    // --- requirement 12: Response Curve ---
    ResponseCurve responseCurve = ResponseCurve::Linear;
    double responseCurveIntensity = 0.0; // 0 = neutral regardless of curve type
    double customCurveExponent = 1.0;    // shaping parameter for ResponseCurve::Custom

    // --- requirement 13: Polling Rate Compensation ---
    double pollingCompensation = 0.0;

    // --- requirement 14: Event/Packet Loss Compensation ---
    double eventLossCompensation = 0.0;

    double mouseDpi = 800.0;
    bool enabled = true;
};

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// Maps a 0-100 slider to filter parameters. Exposed so the UI can show
// derived values if desired, and so unit tests / tuning tools can call them
// directly.
double IntensityToMinCutoff(double intensity0to100);
double IntensityToBeta(double intensity0to100, double dpi);
double SpikeIntensityToMultiplier(double intensity0to100);

// Detects and clamps clearly anomalous single-sample spikes (sensor glitches,
// USB packet corruption). It never predicts or substitutes a "corrected"
// value -- it only limits magnitude, preserving direction exactly, and only
// when a sample is a large outlier relative to recent movement. Normal
// movement, including fast flicks, is always left untouched.
class SpikeGuard {
public:
    // Returns true if this sample was clamped (i.e. was a detected spike).
    bool Process(double& dx, double& dy, double multiplier);
    void Reset();

private:
    bool initialized_ = false;
    double avgMag_ = 0.0;
};

// Dedicated dead-band for requirement 2 (Anti-Jitter), independent from the
// speed-adaptive filter. Sub-threshold motion is accumulated rather than
// discarded, so slow deliberate movement that happens to arrive in tiny
// per-report steps is never permanently lost -- only true back-and-forth
// jitter (which cancels itself out in the carry accumulator) is suppressed.
class AntiJitterFilter {
public:
    double Process(double delta, double thresholdCounts);
    void Reset();

private:
    double carry_ = 0.0;
};

// Tracks short-term instability of the incoming signal (requirement 10:
// Adaptive Noise Reduction) so the pipeline can automatically widen
// smoothing when the sensor/link looks noisy, without the user manually
// raising the base intensity (which would also slow down clean, fast input).
class NoiseVarianceEstimator {
public:
    // Returns a normalized instability score in [0, 1].
    double Update(double magnitude);
    void Reset();

private:
    bool initialized_ = false;
    double emaMag_ = 0.0;
    double emaVar_ = 0.0;
};

// Detects fast flicks and, for a brief settle window afterward, applies
// extra damping to the pipeline's own output to remove overshoot/ringing
// (requirement 6). Only ever attenuates existing motion -- it cannot add
// movement.
class FlickStabilizer {
public:
    Vec2 Process(double x, double y, double dt, double intensity0to100, double dpi);
    void Reset();

private:
    bool initialized_ = false;
    double prevSpeed_ = 0.0;
    double peakSpeed_ = 0.0;
    double settleRemaining_ = 0.0;
    double dampedX_ = 0.0;
    double dampedY_ = 0.0;
};

// Lightweight linear-trend (Holt-style double exponential) predictor used
// for requirement 9 (Motion Prediction). It only extrapolates the user's
// own recently observed velocity trend a small fraction of one report
// ahead -- it is not capable of originating movement on its own, and its
// contribution is capped relative to the current sample so it can never
// dominate or run away.
class MotionPredictor {
public:
    double Process(double delta, double dt, double intensity0to100);
    void Reset();

private:
    bool initialized_ = false;
    double level_ = 0.0;
    double trend_ = 0.0;
};

struct DiagnosticsSnapshot {
    double noiseScore = 0.0;        // 0..1, higher = noisier recent input
    bool spikeClamped = false;      // true if this sample was an anti-spike outlier
    bool eventLossDetected = false; // true if this sample followed a dropped-report gap
    double outputSpeedCountsPerSec = 0.0;
};

// The full per-event pipeline. Stateless with respect to game/screen/process
// context: this class only ever sees (dx, dy, dt) numbers from the mouse and
// returns (dx, dy) numbers to move the cursor by. It has no notion of
// targets, windows, or games, and never reads or writes anything outside
// the input stream itself.
//
// Pipeline order: polling/event-loss timing compensation -> anti-spike ->
// anti-jitter deadband -> snap angle -> speed-adaptive per-axis filtering
// (with straight-line/horizontal/vertical/high-DPI/adaptive-noise coupling)
// -> flick settle -> motion prediction -> acceleration/response-curve gain
// -> flat sensitivity gain.
class MouseFilterPipeline {
public:
    void UpdateSettings(const FilterSettings& settings);
    Vec2 Process(double dx, double dy, double dtSeconds);
    void Reset();

    // Returns a snapshot copy of the diagnostics computed during the most
    // recent Process() call. Not synchronized: only call this from the same
    // thread that calls Process() (the input thread already does, right
    // after each call, to publish these values into SharedState for the UI).
    DiagnosticsSnapshot GetLastDiagnostics() const { return lastDiagnostics_; }

private:
    double ComputeEffectiveDt(double dt);
    double ComputeGain(double outSpeedCountsPerSec) const;
    bool lastEventLossDetected_ = false;

    FilterSettings settings_;
    AdaptiveDeltaFilter xFilter_;
    AdaptiveDeltaFilter yFilter_;
    SpikeGuard spikeGuard_;
    AntiJitterFilter antiJitterX_;
    AntiJitterFilter antiJitterY_;
    NoiseVarianceEstimator noiseEstimator_;
    FlickStabilizer flickStabilizer_;
    MotionPredictor predictorX_;
    MotionPredictor predictorY_;

    // Smoothed per-axis speed magnitudes, reused to decide which axis is
    // "dominant" for straight-line stabilization without extra trig calls.
    double emaSpeedX_ = 0.0;
    double emaSpeedY_ = 0.0;

    // requirement 13/14: running estimate of the "normal" inter-report
    // interval, used to smooth out polling jitter and to recognize dropped
    // events (a gap much larger than expected).
    bool dtInitialized_ = false;
    double expectedDt_ = 1.0 / 500.0; // seed at a plausible 500 Hz until measured

    DiagnosticsSnapshot lastDiagnostics_;
};

} // namespace pmf
