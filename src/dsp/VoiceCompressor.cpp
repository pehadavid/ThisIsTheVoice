// SPDX-License-Identifier: GPL-3.0-or-later
#include "VoiceCompressor.hpp"

#include <algorithm>
#include <cmath>

namespace titv::dsp {

namespace {
constexpr float kAmountSmoothingSeconds = 0.050f;
constexpr float kRmsAttackSeconds = 0.020f;
constexpr float kRmsReleaseSeconds = 0.200f;
constexpr float kPeakAttackSeconds = 0.003f;
constexpr float kPeakReleaseSeconds = 0.080f;
constexpr float kLimiterAttackSeconds = 0.0003f;
constexpr float kLimiterReleaseSeconds = 0.050f;
} // namespace

VoiceCompressor::Settings VoiceCompressor::settingsFor(float a) noexcept
{
    a = std::clamp(a, 0.0f, 1.0f);
    return {
        -30.0f, 1.0f + 3.0f * a, 0.5f * a,
        -10.0f - 18.0f * a, 1.0f + 4.0f * a,
        std::min(1.0f, 20.0f * a),
    };
}

float VoiceCompressor::parallelMakeupDb(const Settings& s) noexcept
{
    return -compressorGainDb(kReferenceRmsDb, s.parallelThresholdDb, s.parallelRatio, kParallelKneeDb);
}

float VoiceCompressor::serialMakeupDb(const Settings& s) noexcept
{
    return -compressorGainDb(kReferencePeakDb, s.serialThresholdDb, s.serialRatio, kSerialKneeDb);
}

void VoiceCompressor::prepare(double sampleRate)
{
    amount_.prepare(kAmountSmoothingSeconds, sampleRate / kControlInterval);
    rmsEnv_.prepare(kRmsAttackSeconds, kRmsReleaseSeconds, sampleRate);
    peakEnv_.prepare(kPeakAttackSeconds, kPeakReleaseSeconds, sampleRate);
    limiterAttack_ = onePoleCoeff(kLimiterAttackSeconds, sampleRate);
    limiterRelease_ = onePoleCoeff(kLimiterReleaseSeconds, sampleRate);
    limiterCeiling_ = dbToGain(kLimiterCeilingDb);
    reset();
}

void VoiceCompressor::reset() noexcept
{
    amount_.snap(amount_.target());
    rmsEnv_.reset();
    peakEnv_.reset();
    limiterEnv_.reset();
    blend_ = serialGain_ = 1.0f;
    blendStep_ = serialStep_ = 0.0f;
    serialReductionDb_ = 0.0f;
    countdown_ = 0;
    reductionOut_.store(0.0f, std::memory_order_relaxed);
    updateControl();
    blend_ = blendTarget_;
    serialGain_ = serialTarget_;
    blendStep_ = serialStep_ = 0.0f;
}

void VoiceCompressor::updateControl() noexcept
{
    const float a = amount_.next();
    settings_ = settingsFor(a);
    parallelMakeupDb_ = parallelMakeupDb(settings_);
    serialMakeupDb_ = serialMakeupDb(settings_);

    // Stage 1: y = x * (1 + mix * (g - 1)), where g is the compressed path's gain.
    float blend = 1.0f;
    if (settings_.parallelMix > 0.0f) {
        const float gr = compressorGainDb(powerDb(rmsEnv_.value()), settings_.parallelThresholdDb,
                                          settings_.parallelRatio, kParallelKneeDb);
        blend = 1.0f + settings_.parallelMix * (dbToGain(gr + parallelMakeupDb_) - 1.0f);
    }

    // Stage 2.
    float serial = 1.0f;
    serialReductionDb_ = 0.0f;
    if (settings_.serialRatio > 1.0f) {
        const float gr = compressorGainDb(levelDb(peakEnv_.value()), settings_.serialThresholdDb,
                                          settings_.serialRatio, kSerialKneeDb);
        serialReductionDb_ = -gr;
        serial = dbToGain(gr + serialMakeupDb_);
    }

    blendTarget_ = blend;
    serialTarget_ = serial;
    blendStep_ = (blend - blend_) / kControlInterval;
    serialStep_ = (serial - serialGain_) / kControlInterval;
}

void VoiceCompressor::process(float* left, float* right, uint32_t frames) noexcept
{
    float minLimiterGain = 1.0f;
    float maxSerialReduction = 0.0f;

    for (uint32_t n = 0; n < frames; ++n) {
        if (countdown_ == 0) {
            updateControl();
            countdown_ = kControlInterval;
            maxSerialReduction = std::max(maxSerialReduction, serialReductionDb_);
        }
        --countdown_;
        if (countdown_ == 0) {
            // Land exactly on the targets, free of accumulated rounding.
            blend_ = blendTarget_;
            serialGain_ = serialTarget_;
        } else {
            blend_ += blendStep_;
            serialGain_ += serialStep_;
        }

        float l = left[n], r = right[n];

        // Stage 1: parallel. The detector sees the section input.
        rmsEnv_.process(0.5f * (l * l + r * r));
        l *= blend_;
        r *= blend_;

        // Stage 2: serial, detecting its own input.
        peakEnv_.process(std::max(std::fabs(l), std::fabs(r)));
        l *= serialGain_;
        r *= serialGain_;

        // Stage 3: limiter then soft clip, blended in with the macro.
        if (settings_.limiterMix > 0.0f) {
            const float peak = std::max(std::fabs(l), std::fabs(r));
            const float e = limiterEnv_.value();
            const float coeff = peak > e ? limiterAttack_ : limiterRelease_;
            const float env = coeff * e + (1.0f - coeff) * peak;
            limiterEnv_.reset(env);
            const float g = env > limiterCeiling_ ? limiterCeiling_ / env : 1.0f;
            minLimiterGain = std::min(minLimiterGain, g);
            const float m = settings_.limiterMix;
            l += m * (softClip(l * g, kClipKnee) - l);
            r += m * (softClip(r * g, kClipKnee) - r);
        }

        left[n] = l;
        right[n] = r;
    }

    reductionOut_.store(maxSerialReduction - gainToDb(minLimiterGain), std::memory_order_relaxed);
}

} // namespace titv::dsp
