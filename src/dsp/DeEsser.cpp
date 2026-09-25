// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeEsser.hpp"

#include <algorithm>
#include <cmath>

namespace titv::dsp {

namespace {
constexpr float kAmountSmoothingSeconds = 0.030f;
constexpr float kDetectorAttackSeconds = 0.0005f;
constexpr float kDetectorReleaseSeconds = 0.040f;
constexpr float kReductionAttackSeconds = 0.001f;
constexpr float kReductionReleaseSeconds = 0.060f;
} // namespace

void DeEsser::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    const double controlRate = sampleRate / kControlInterval;
    amount_.prepare(kAmountSmoothingSeconds, controlRate);
    detectorHighPass_.setCoeffs(BiquadCoeffs::highPass(kDetectorHighPassHz, 0.707, sampleRate));
    detector_.setCoeffs(BiquadCoeffs::bandPass(kFrequencyHz, kDetectorQ, sampleRate));
    bandEnv_.prepare(kDetectorAttackSeconds, kDetectorReleaseSeconds, sampleRate);
    fullEnv_.prepare(kDetectorAttackSeconds, kDetectorReleaseSeconds, sampleRate);
    reduction_.prepare(kReductionAttackSeconds, kReductionReleaseSeconds, controlRate);
    reset();
}

void DeEsser::reset() noexcept
{
    amount_.snap(amount_.target());
    detectorHighPass_.reset();
    detector_.reset();
    bandEnv_.reset();
    fullEnv_.reset();
    reduction_.reset();
    for (Biquad& f : cut_) {
        f.reset();
        f.setCoeffs(BiquadCoeffs {});
    }
    appliedDb_ = 0.0f;
    countdown_ = 0;
    reductionOut_.store(0.0f, std::memory_order_relaxed);
}

void DeEsser::updateGain() noexcept
{
    const float amount = amount_.next();
    const float full = fullEnv_.value();

    float target = 0.0f;
    if (amount > 0.0f && levelDb(full) > kGateDb) {
        const float ratioDb = levelDb(bandEnv_.value()) - levelDb(full);
        target = std::min(-compressorGainDb(ratioDb, thresholdDb(amount), kRatio, kKneeDb), maxReductionDb(amount));
    }
    const float reduction = reduction_.process(target);

    if (std::fabs(reduction - appliedDb_) >= 0.01f || (reduction == 0.0f && appliedDb_ != 0.0f)) {
        appliedDb_ = reduction < 0.01f ? 0.0f : reduction;
        const BiquadCoeffs c = appliedDb_ == 0.0f ? BiquadCoeffs {}
                                                  : BiquadCoeffs::peaking(kFrequencyHz, kCutQ, -appliedDb_, sampleRate_);
        for (Biquad& f : cut_) {
            f.setCoeffs(c);
            // Below 0.01 dB the residual state is negligible; clearing it lets the
            // filter restart cleanly on the next sibilant.
            if (appliedDb_ == 0.0f)
                f.reset();
        }
    }
}

void DeEsser::process(float* left, float* right, uint32_t frames) noexcept
{
    // At 0 % with no reduction left, the detector rests too.
    if (amount_.isSettled() && amount_.target() == 0.0f && appliedDb_ == 0.0f) {
        idle_ = true;
        reductionOut_.store(0.0f, std::memory_order_relaxed);
        return;
    }
    if (idle_) {
        idle_ = false;
        detectorHighPass_.reset();
        detector_.reset();
        bandEnv_.reset();
        fullEnv_.reset();
        reduction_.reset();
    }
    for (uint32_t n = 0; n < frames; ++n) {
        if (countdown_ == 0) {
            updateGain();
            countdown_ = kControlInterval;
        }
        --countdown_;

        const float mid = 0.5f * (left[n] + right[n]);
        bandEnv_.process(std::fabs(detector_.process(detectorHighPass_.process(mid))));
        fullEnv_.process(std::fabs(mid));

        // No reduction: the signal passes untouched (the cut filters keep identity
        // coefficients, so their state stays at zero).
        if (appliedDb_ != 0.0f) {
            left[n] = cut_[0].process(left[n]);
            right[n] = cut_[1].process(right[n]);
        }
    }
    reductionOut_.store(appliedDb_, std::memory_order_relaxed);
}

} // namespace titv::dsp
