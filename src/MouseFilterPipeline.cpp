#include "MouseFilterPipeline.h"
#include <algorithm>
#include <cmath>

namespace pmf {
namespace {

// --- Tuning ranges -----------------------------------------------------
// Principled starting points derived from the filter math, meant to be
// adjusted to taste via the UI sliders -- not claimed to be universally
// "perfect".

constexpr double kMinCutoffFloor = 0.04; // Hz, strongest smoothing (intensity = 100)
constexpr double kMinCutoffCeil  = 6.0;  // Hz, lightest smoothing (intensity = 0)

constexpr double kBetaFloor = 0.0008; // slowest to relax -> smoothing persists into faster motion
constexpr double kBetaCeil  = 0.04;   // fastest to relax -> smoothing backs off almost immediately
constexpr double kReferenceDpi = 800.0;

constexpr double kSpikeMultiplierFloor = 4.0;  // most aggressive clamp (sensitivity = 100)
constexpr double kSpikeMultiplierCeil  = 12.0; // most conservative clamp (sensitivity = 0)
constexpr double kSpikeAbsoluteFloorCounts = 15.0; // never treat tiny movements as "spikes"
constexpr double kSpikeEmaWeight = 0.9;

// Ratio of minor-axis to dominant-axis smoothed speed. Below the "floor" the
// motion is considered clearly axis-aligned (full extra damping available);
// above the "ceil" it is considered a deliberate diagonal (no extra damping).
constexpr double kStraightLineDominanceFloor = 0.06;
constexpr double kStraightLineDominanceCeil  = 0.35;
constexpr double kStraightLineMaxCutoffReduction = 0.85; // never fully zero the minor axis

// requirement 2: Anti-Jitter dead-band, in mouse counts at the reference
// DPI. Scaled by DPI the same way beta is, so the felt dead-zone size is
// DPI-independent.
constexpr double kAntiJitterMaxCounts = 2.5;

// requirement 4: Snap Angle. Motion must already be within this many degrees
// of a 45-degree compass direction before any rotation is applied; the
// slider only controls how strongly it pulls within that tolerance, never
// how wide the tolerance itself is.
constexpr double kSnapToleranceDeg = 6.0;

// requirement 5: extra minCutoff reduction applied on top of DPI-normalized
// beta, once DPI exceeds the reference.
constexpr double kHighDpiMaxReduction = 0.5;

// requirement 6: Flick Stabilizer.
constexpr double kFlickTriggerCountsPerSec = 3500.0; // at reference DPI
constexpr double kFlickDecelerationRatio = 0.55;     // speed must drop below this fraction of the peak
constexpr double kFlickSettleMaxSeconds = 0.12;
constexpr double kFlickSettleAlpha = 0.55; // low-pass weight applied during the settle window

// requirements 7/8: max extra minCutoff reduction from the per-axis sliders.
constexpr double kAxisStabilizerMaxReduction = 0.6;

// requirement 9: Motion Prediction. The predicted contribution is capped to
// this fraction of the current sample's own magnitude so it can only ever
// nudge the existing motion forward slightly, never dominate it.
constexpr double kMotionPredictionMaxLookaheadSeconds = 0.010;
constexpr double kMotionPredictionMaxFraction = 0.35;
constexpr double kMotionPredictionTrendAlpha = 0.25;

// requirement 10: Adaptive Noise Reduction.
constexpr double kNoiseVarianceAlpha = 0.12;
constexpr double kNoiseInstabilityReferenceCounts = 60.0; // stdev at which score saturates to 1.0
constexpr double kAdaptiveNoiseMaxReduction = 0.5;

// requirement 11/12: acceleration + response curve gain shaping.
constexpr double kAccelReferenceCountsPerSec = 4000.0; // speed at which speedNorm reaches ~1.0
constexpr double kAccelMaxGainDelta = 0.6;   // +-60% at full slider + full speed
constexpr double kGainClampMin = 0.25;
constexpr double kGainClampMax = 2.5;

// requirements 13/14: polling-rate & event-loss compensation.
constexpr double kExpectedDtAlpha = 0.05;         // how fast the "normal interval" estimate adapts
constexpr double kEventLossGapMultiplier = 3.0;    // dt beyond this multiple of expectedDt_ is a gap
constexpr double kEventLossCapMultiplier = 1.5;    // gaps are capped to this multiple for filter timing
constexpr double kMinDt = 1e-4;
constexpr double kMaxPlausibleDt = 0.5; // guards against absurd first-sample / resume-from-sleep dt

} // namespace

double IntensityToMinCutoff(double t) {
    t = std::clamp(t, 0.0, 100.0) / 100.0;
    // Log-scale interpolation: cutoff frequency perception/response is
    // naturally logarithmic, so a linear slider maps to it exponentially.
    return kMinCutoffCeil * std::pow(kMinCutoffFloor / kMinCutoffCeil, t);
}

double IntensityToBeta(double t, double dpi) {
    t = std::clamp(t, 0.0, 100.0) / 100.0;
    double beta = kBetaCeil * std::pow(kBetaFloor / kBetaCeil, t);
    // Normalize by DPI so the same slider position feels the same regardless
    // of the mouse's configured sensor DPI (higher DPI => more counts for the
    // same physical hand speed => beta must shrink proportionally).
    double dpiScale = kReferenceDpi / std::max(dpi, 100.0);
    return beta * dpiScale;
}

double SpikeIntensityToMultiplier(double t) {
    t = std::clamp(t, 0.0, 100.0) / 100.0;
    return kSpikeMultiplierCeil + (kSpikeMultiplierFloor - kSpikeMultiplierCeil) * t;
}

bool SpikeGuard::Process(double& dx, double& dy, double multiplier) {
    double mag = std::sqrt(dx * dx + dy * dy);
    bool clamped = false;
    if (initialized_) {
        double limit = avgMag_ * multiplier + kSpikeAbsoluteFloorCounts;
        if (mag > limit && mag > kSpikeAbsoluteFloorCounts * 2.0) {
            double scale = limit / mag;
            dx *= scale;
            dy *= scale;
            mag = limit; // update the running average with the clamped value,
                         // so one spike cannot permanently poison the baseline
            clamped = true;
        }
    } else {
        initialized_ = true;
    }
    avgMag_ = avgMag_ * kSpikeEmaWeight + mag * (1.0 - kSpikeEmaWeight);
    return clamped;
}

void SpikeGuard::Reset() {
    initialized_ = false;
    avgMag_ = 0.0;
}

double AntiJitterFilter::Process(double delta, double thresholdCounts) {
    if (thresholdCounts <= 0.0) {
        carry_ = 0.0;
        return delta;
    }
    double combined = carry_ + delta;
    if (std::fabs(combined) < thresholdCounts) {
        carry_ = combined;
        return 0.0;
    }
    carry_ = 0.0;
    return combined;
}

void AntiJitterFilter::Reset() {
    carry_ = 0.0;
}

double NoiseVarianceEstimator::Update(double magnitude) {
    if (!initialized_) {
        emaMag_ = magnitude;
        emaVar_ = 0.0;
        initialized_ = true;
        return 0.0;
    }
    double diff = magnitude - emaMag_;
    emaMag_ += kNoiseVarianceAlpha * diff;
    emaVar_ += kNoiseVarianceAlpha * (diff * diff - emaVar_);
    double stdev = std::sqrt(std::max(emaVar_, 0.0));
    return std::clamp(stdev / kNoiseInstabilityReferenceCounts, 0.0, 1.0);
}

void NoiseVarianceEstimator::Reset() {
    initialized_ = false;
    emaMag_ = 0.0;
    emaVar_ = 0.0;
}

Vec2 FlickStabilizer::Process(double x, double y, double dt, double intensity0to100, double dpi) {
    if (intensity0to100 <= 0.0) {
        Reset();
        return Vec2{x, y};
    }
    double safeDt = std::max(dt, kMinDt);
    double speed = std::sqrt(x * x + y * y) / safeDt;
    double dpiScale = std::max(dpi, 100.0) / kReferenceDpi;
    double triggerSpeed = kFlickTriggerCountsPerSec * dpiScale;

    if (!initialized_) {
        prevSpeed_ = speed;
        peakSpeed_ = speed;
        dampedX_ = x;
        dampedY_ = y;
        initialized_ = true;
        return Vec2{x, y};
    }

    if (speed > peakSpeed_) {
        peakSpeed_ = speed;
    }
    // A settle window opens once we were near/above the flick trigger speed
    // and have since decelerated sharply -- that deceleration is exactly the
    // moment overshoot/ringing becomes perceptible.
    if (peakSpeed_ > triggerSpeed && speed < peakSpeed_ * kFlickDecelerationRatio &&
        settleRemaining_ <= 0.0) {
        settleRemaining_ = kFlickSettleMaxSeconds * (intensity0to100 / 100.0);
    }

    Vec2 out{x, y};
    if (settleRemaining_ > 0.0) {
        double alpha = kFlickSettleAlpha * (intensity0to100 / 100.0) *
                       std::clamp(settleRemaining_ / kFlickSettleMaxSeconds, 0.0, 1.0);
        dampedX_ = dampedX_ + alpha * (x - dampedX_);
        dampedY_ = dampedY_ + alpha * (y - dampedY_);
        out.x = dampedX_;
        out.y = dampedY_;
        settleRemaining_ -= safeDt;
        if (settleRemaining_ <= 0.0) {
            peakSpeed_ = speed; // settle window closed; re-arm for the next flick
        }
    } else {
        dampedX_ = x;
        dampedY_ = y;
    }

    prevSpeed_ = speed;
    return out;
}

void FlickStabilizer::Reset() {
    initialized_ = false;
    prevSpeed_ = 0.0;
    peakSpeed_ = 0.0;
    settleRemaining_ = 0.0;
    dampedX_ = 0.0;
    dampedY_ = 0.0;
}

double MotionPredictor::Process(double delta, double dt, double intensity0to100) {
    if (intensity0to100 <= 0.0) {
        Reset();
        return delta;
    }
    double safeDt = std::max(dt, kMinDt);
    if (!initialized_) {
        level_ = delta;
        trend_ = 0.0;
        initialized_ = true;
        return delta;
    }

    double prevLevel = level_;
    level_ = delta; // the level always tracks the latest actual sample exactly
    double instantaneousTrend = (level_ - prevLevel) / safeDt;
    trend_ += kMotionPredictionTrendAlpha * (instantaneousTrend - trend_);

    double lookahead = std::min(safeDt, kMotionPredictionMaxLookaheadSeconds);
    double predicted = trend_ * lookahead * (intensity0to100 / 100.0);

    // Cap the contribution relative to the sample itself: this can only ever
    // nudge motion that is already happening, never originate its own.
    double cap = std::fabs(delta) * kMotionPredictionMaxFraction;
    predicted = std::clamp(predicted, -cap, cap);

    return delta + predicted;
}

void MotionPredictor::Reset() {
    initialized_ = false;
    level_ = 0.0;
    trend_ = 0.0;
}

namespace {

// requirement 12: shapes a normalized speed [0,1] into a gain multiplier
// centered on 1.0. Each curve is defined so that intensity=0 always yields
// exactly 1.0 (neutral), independent of curve type.
double ShapeCurve(double speedNorm, ResponseCurve curve, double exponent, double intensity01) {
    speedNorm = std::clamp(speedNorm, 0.0, 1.0);
    double shaped; // in [0, 1], to be recentered around 1.0 below
    switch (curve) {
        case ResponseCurve::Exponential:
            shaped = speedNorm * speedNorm;
            break;
        case ResponseCurve::Smooth:
            // Smoothstep -- gentle ease-in/ease-out, avoids abrupt gain changes.
            shaped = speedNorm * speedNorm * (3.0 - 2.0 * speedNorm);
            break;
        case ResponseCurve::Custom: {
            double e = std::clamp(exponent, 0.1, 5.0);
            shaped = std::pow(speedNorm, e);
            break;
        }
        case ResponseCurve::Linear:
        default:
            shaped = speedNorm;
            break;
    }
    // Recenter so 0 speed -> 1.0 and full speed -> 1.0 + intensity-scaled
    // deviation; "shaped - speedNorm" isolates just the curve's own shaping
    // relative to a plain linear ramp, so intensity=0 is always exactly
    // neutral regardless of curve type.
    double deviation = (shaped - speedNorm);
    return 1.0 + deviation * intensity01;
}

} // namespace

double MouseFilterPipeline::ComputeGain(double outSpeedCountsPerSec) const {
    double dpiScale = std::max(settings_.mouseDpi, 100.0) / kReferenceDpi;
    double speedNorm = std::clamp(outSpeedCountsPerSec / (kAccelReferenceCountsPerSec * dpiScale), 0.0, 1.0);

    double gain = 1.0;

    // requirement 11: acceleration control, symmetric around 0.
    if (settings_.accelerationControl != 0.0) {
        double accel01 = std::clamp(settings_.accelerationControl, -100.0, 100.0) / 100.0;
        gain *= 1.0 + accel01 * kAccelMaxGainDelta * speedNorm;
    }

    // requirement 12: response curve, blended in by its own intensity.
    if (settings_.responseCurveIntensity > 0.0) {
        double intensity01 = std::clamp(settings_.responseCurveIntensity, 0.0, 100.0) / 100.0;
        gain *= ShapeCurve(speedNorm, settings_.responseCurve, settings_.customCurveExponent, intensity01);
    }

    return std::clamp(gain, kGainClampMin, kGainClampMax);
}

double MouseFilterPipeline::ComputeEffectiveDt(double dt) {
    double rawDt = std::clamp(dt, kMinDt, kMaxPlausibleDt);

    if (!dtInitialized_) {
        expectedDt_ = rawDt;
        dtInitialized_ = true;
        lastEventLossDetected_ = false;
        return rawDt;
    }

    bool isGap = rawDt > expectedDt_ * kEventLossGapMultiplier;
    lastEventLossDetected_ = isGap;

    // requirement 14: event/packet-loss compensation. A dropped report makes
    // dt look huge, which would make the adaptive filter briefly think
    // motion has nearly stopped (speed = distance / dt underestimated) right
    // before the next, now-larger, delta arrives. Capping the dt used for
    // filter timing (never the real dx/dy, which are left untouched so
    // on-screen distance is always correct) prevents that misread.
    double dtForFilter = rawDt;
    if (isGap && settings_.eventLossCompensation > 0.0) {
        double intensity01 = std::clamp(settings_.eventLossCompensation, 0.0, 100.0) / 100.0;
        double cappedDt = std::min(rawDt, expectedDt_ * kEventLossCapMultiplier);
        dtForFilter = rawDt + (cappedDt - rawDt) * intensity01;
    }

    // requirement 13: polling-rate compensation blends the instantaneous
    // interval toward the recently-observed average, so small jitter in the
    // reporting interval (common with wireless/USB polling) doesn't itself
    // masquerade as a speed change to the adaptive filter.
    if (!isGap && settings_.pollingCompensation > 0.0) {
        double intensity01 = std::clamp(settings_.pollingCompensation, 0.0, 100.0) / 100.0;
        dtForFilter = dtForFilter + (expectedDt_ - dtForFilter) * (intensity01 * 0.8);
    }

    if (!isGap) {
        expectedDt_ += kExpectedDtAlpha * (rawDt - expectedDt_);
    }

    return std::max(dtForFilter, kMinDt);
}

void MouseFilterPipeline::UpdateSettings(const FilterSettings& s) {
    settings_ = s;
}

Vec2 MouseFilterPipeline::Process(double dx, double dy, double dtIn) {
    if (!settings_.enabled) {
        // Passthrough still applies the flat sensitivity multiplier so
        // toggling the filter does not also change overall pointer speed.
        return Vec2{dx * settings_.sensitivity, dy * settings_.sensitivity};
    }

    double dt = ComputeEffectiveDt(dtIn);

    // 1) Anti-spike: clamp only clear sensor outliers (requirement 3).
    double spikeMultiplier = SpikeIntensityToMultiplier(settings_.antiSpikeSensitivity);
    bool spikeClamped = spikeGuard_.Process(dx, dy, spikeMultiplier);

    // 2) Anti-jitter dead-band (requirement 2), independent of the
    //    speed-adaptive filter that follows.
    if (settings_.antiJitterIntensity > 0.0) {
        double dpiScale = kReferenceDpi / std::max(settings_.mouseDpi, 100.0);
        double thresholdCounts =
            (settings_.antiJitterIntensity / 100.0) * kAntiJitterMaxCounts * dpiScale;
        dx = antiJitterX_.Process(dx, thresholdCounts);
        dy = antiJitterY_.Process(dy, thresholdCounts);
    } else {
        antiJitterX_.Reset();
        antiJitterY_.Reset();
    }

    // 3) Snap angle (requirement 4): only rotates motion that is already
    //    close to a 45-degree compass direction, and only by an amount
    //    proportional to both the slider and how close it already is.
    if (settings_.snapAngleIntensity > 0.0) {
        double mag = std::sqrt(dx * dx + dy * dy);
        if (mag > 1e-6) {
            double angle = std::atan2(dy, dx);
            double step = kPi / 4.0;
            double nearest = std::round(angle / step) * step;
            double diff = angle - nearest;
            // wrap to [-pi, pi] (already within +-pi/8 by construction, so
            // this is just defensive)
            while (diff > kPi) diff -= 2.0 * kPi;
            while (diff < -kPi) diff += 2.0 * kPi;

            double toleranceRad = kSnapToleranceDeg * kPi / 180.0;
            double absDiff = std::fabs(diff);
            if (absDiff < toleranceRad) {
                double closeness = 1.0 - (absDiff / toleranceRad);
                double strength = (settings_.snapAngleIntensity / 100.0) * closeness;
                double newAngle = angle - diff * strength;
                dx = mag * std::cos(newAngle);
                dy = mag * std::sin(newAngle);
            }
        }
    }

    // 4) Track smoothed per-axis speed for straight-line coupling
    //    (existing behavior) and for adaptive noise reduction (requirement 10).
    double absDx = std::fabs(dx) / dt;
    double absDy = std::fabs(dy) / dt;
    emaSpeedX_ = emaSpeedX_ * 0.8 + absDx * 0.2;
    emaSpeedY_ = emaSpeedY_ * 0.8 + absDy * 0.2;

    double dominant = std::max(emaSpeedX_, emaSpeedY_);
    double minor = std::min(emaSpeedX_, emaSpeedY_);
    double ratio = dominant > 1e-6 ? (minor / dominant) : 1.0;

    double straightLine01 = std::clamp(settings_.straightLineIntensity, 0.0, 100.0) / 100.0;
    double tRatio = std::clamp(
        (kStraightLineDominanceCeil - ratio) /
        (kStraightLineDominanceCeil - kStraightLineDominanceFloor),
        0.0, 1.0);
    // extraDamping is 0 for deliberate diagonals (never "corrects" them) and
    // rises smoothly (no snapping) for clearly axis-aligned motion.
    double extraDamping = tRatio * straightLine01;

    double noise01 = noiseEstimator_.Update(std::sqrt(dx * dx + dy * dy));

    double baseMinCutoff = IntensityToMinCutoff(settings_.filterIntensity);
    double baseBeta = IntensityToBeta(settings_.smoothingIntensity, settings_.mouseDpi);

    // requirement 5: extra reduction once DPI exceeds the reference.
    double dpiRatio = std::max(settings_.mouseDpi, 100.0) / kReferenceDpi;
    double highDpiExtra = 0.0;
    if (settings_.highSensitivityFilterIntensity > 0.0 && dpiRatio > 1.0) {
        double dpiExcess = std::clamp((dpiRatio - 1.0) / 3.0, 0.0, 1.0); // saturates ~4x reference DPI
        highDpiExtra = dpiExcess * (settings_.highSensitivityFilterIntensity / 100.0) * kHighDpiMaxReduction;
    }

    // requirement 10: extra reduction proportional to measured instability.
    double adaptiveExtra = 0.0;
    if (settings_.adaptiveNoiseReduction > 0.0) {
        adaptiveExtra = noise01 * (settings_.adaptiveNoiseReduction / 100.0) * kAdaptiveNoiseMaxReduction;
    }

    double sharedReduction = std::clamp(highDpiExtra + adaptiveExtra, 0.0, 0.9);
    double sharedCutoff = std::max(baseMinCutoff * (1.0 - sharedReduction), kMinCutoffFloor * 0.5);

    double dampedCutoff = std::max(
        sharedCutoff * (1.0 - kStraightLineMaxCutoffReduction * extraDamping),
        kMinCutoffFloor * 0.5);

    // requirements 7/8: independent per-axis extra stabilization.
    double xCutoffFloorScale = 1.0 - (settings_.horizontalStabilizerIntensity / 100.0) * kAxisStabilizerMaxReduction;
    double yCutoffFloorScale = 1.0 - (settings_.verticalStabilizerIntensity / 100.0) * kAxisStabilizerMaxReduction;

    double xMinCutoff, yMinCutoff;
    // Only the minor axis of THIS sample gets the straight-line extra
    // damping; the dominant axis (the user's intended direction of travel)
    // is never touched by it. The per-axis stabilizer sliders, in contrast,
    // apply unconditionally to their axis.
    if (emaSpeedX_ < emaSpeedY_) {
        xMinCutoff = dampedCutoff * xCutoffFloorScale;
        yMinCutoff = sharedCutoff * yCutoffFloorScale;
    } else {
        yMinCutoff = dampedCutoff * yCutoffFloorScale;
        xMinCutoff = sharedCutoff * xCutoffFloorScale;
    }
    xMinCutoff = std::max(xMinCutoff, kMinCutoffFloor * 0.25);
    yMinCutoff = std::max(yMinCutoff, kMinCutoffFloor * 0.25);

    xFilter_.SetParameters(xMinCutoff, baseBeta);
    yFilter_.SetParameters(yMinCutoff, baseBeta);

    // 5) Speed-adaptive smoothing itself.
    double outX = xFilter_.Filter(dx, dt);
    double outY = yFilter_.Filter(dy, dt);

    // 6) Flick stabilizer settle window (requirement 6).
    Vec2 settled = flickStabilizer_.Process(outX, outY, dt, settings_.flickStabilizerIntensity,
                                             settings_.mouseDpi);
    outX = settled.x;
    outY = settled.y;

    // 7) Motion prediction (requirement 9), applied last before gain so it
    //    extrapolates the pipeline's own already-smoothed output.
    outX = predictorX_.Process(outX, dt, settings_.motionPredictionIntensity);
    outY = predictorY_.Process(outY, dt, settings_.motionPredictionIntensity);

    // 8) Acceleration control + response curve gain (requirements 11, 12),
    //    then the flat, otherwise speed-independent sensitivity multiplier.
    double outSpeed = std::sqrt(outX * outX + outY * outY) / dt;
    double gain = ComputeGain(outSpeed);

    lastDiagnostics_.noiseScore = noise01;
    lastDiagnostics_.spikeClamped = spikeClamped;
    lastDiagnostics_.eventLossDetected = lastEventLossDetected_;
    lastDiagnostics_.outputSpeedCountsPerSec = outSpeed;

    return Vec2{outX * gain * settings_.sensitivity, outY * gain * settings_.sensitivity};
}

void MouseFilterPipeline::Reset() {
    xFilter_.Reset();
    yFilter_.Reset();
    spikeGuard_.Reset();
    antiJitterX_.Reset();
    antiJitterY_.Reset();
    noiseEstimator_.Reset();
    flickStabilizer_.Reset();
    predictorX_.Reset();
    predictorY_.Reset();
    emaSpeedX_ = emaSpeedY_ = 0.0;
    dtInitialized_ = false;
    expectedDt_ = 1.0 / 500.0;
    lastEventLossDetected_ = false;
    lastDiagnostics_ = DiagnosticsSnapshot{};
}

} // namespace pmf
