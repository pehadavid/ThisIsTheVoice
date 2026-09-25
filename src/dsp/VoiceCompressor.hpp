// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Envelope.hpp"
#include "Smoother.hpp"

#include <atomic>
#include <cstdint>

namespace titv::dsp {

// COMPRESS: one macro drives three stages, all without lookahead.
//   1. Gentle parallel compression: slow RMS detector, low ratio, blended with the
//      uncompressed signal. Its make-up restores the reference level, so the blend
//      mostly lifts quiet passages.
//   2. Serial compression: faster peak detector; threshold and ratio follow the macro.
//      Its make-up keeps a voice at the reference level at a comparable loudness
//      whatever the macro.
//   3. Peak containment: a limiter with a very short attack, then a soft clipper for
//      what the attack lets through.
// At 0 % every stage is exactly neutral.
//
// Stage 1 and 2 gains are computed at control rate and ramped linearly in between;
// the limiter runs per sample.
class VoiceCompressor {
public:
    static constexpr uint32_t kControlInterval = 16;

    struct Settings {
        float parallelThresholdDb, parallelRatio, parallelMix;
        float serialThresholdDb, serialRatio;
        float limiterMix;
    };

    static constexpr float kParallelKneeDb = 12.0f;
    static constexpr float kSerialKneeDb = 8.0f;
    // Reference levels for the make-up gains: a voice sitting in the input meter's
    // target zone (-18 dBFS RMS) and its typical peak-envelope level.
    static constexpr float kReferenceRmsDb = -18.0f;
    static constexpr float kReferencePeakDb = -13.0f;
    static constexpr float kLimiterCeilingDb = -1.5f;
    static constexpr float kClipKnee = 0.85f;

    // Mapping of the macro (0..1) to the stage settings.
    static Settings settingsFor(float amount) noexcept;
    static float parallelMakeupDb(const Settings& s) noexcept;
    static float serialMakeupDb(const Settings& s) noexcept;

    void prepare(double sampleRate);
    void reset() noexcept;

    void setAmount(float amount01) noexcept { amount_.setTarget(amount01); }
    void process(float* left, float* right, uint32_t frames) noexcept;

    // Reduction of the serial stage and limiter over the last block, in dB (>= 0).
    float reductionDb() const noexcept { return reductionOut_.load(std::memory_order_relaxed); }

private:
    void updateControl() noexcept;

    uint32_t countdown_ = 0;
    Smoother amount_;
    Settings settings_ {};
    float parallelMakeupDb_ = 0.0f, serialMakeupDb_ = 0.0f;

    EnvelopeFollower rmsEnv_, peakEnv_, limiterEnv_;
    float limiterRelease_ = 0.0f, limiterAttack_ = 0.0f, limiterCeiling_ = 1.0f;

    // Linear ramps of the stage 1 blend factor and stage 2 gain across a control interval.
    float blend_ = 1.0f, blendStep_ = 0.0f, blendTarget_ = 1.0f;
    float serialGain_ = 1.0f, serialStep_ = 0.0f, serialTarget_ = 1.0f;
    float serialReductionDb_ = 0.0f;

    std::atomic<float> reductionOut_ { 0.0f };
};

} // namespace titv::dsp
