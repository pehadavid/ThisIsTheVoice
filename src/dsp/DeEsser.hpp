// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Biquad.hpp"
#include "Envelope.hpp"
#include "Smoother.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace titv::dsp {

// De-esser: a high-pass + band-pass detector around the sibilance
// range (the high-pass keeps vowel harmonics out of it) drives a
// dynamic bell cut at the same frequency. Detection is relative to the full-band
// level, so the result does not depend on the input gain; the knob lowers the
// threshold and raises the maximum reduction. 0 % leaves the signal untouched.
class DeEsser {
public:
    static constexpr uint32_t kControlInterval = 16;
    static constexpr double kFrequencyHz = 7000.0;
    static constexpr double kDetectorQ = 0.9;
    static constexpr double kDetectorHighPassHz = 4500.0;
    static constexpr double kCutQ = 1.0;
    static constexpr float kRatio = 4.0f;
    static constexpr float kKneeDb = 6.0f;
    static constexpr float kGateDb = -55.0f; // no reduction on near-silence

    // Threshold on the band-to-full level ratio, and maximum cut, for amount 0..1.
    static float thresholdDb(float amount) noexcept { return -3.0f - 15.0f * amount; }
    static float maxReductionDb(float amount) noexcept { return 14.0f * amount; }

    void prepare(double sampleRate);
    void reset() noexcept;

    void setAmount(float amount01) noexcept { amount_.setTarget(amount01); }
    void process(float* left, float* right, uint32_t frames) noexcept;

    // Current reduction in dB (>= 0), readable from any thread.
    float reductionDb() const noexcept { return reductionOut_.load(std::memory_order_relaxed); }

private:
    void updateGain() noexcept;

    double sampleRate_ = 48000.0;
    uint32_t countdown_ = 0;
    Smoother amount_;
    Biquad detectorHighPass_, detector_;
    EnvelopeFollower bandEnv_, fullEnv_, reduction_;
    std::array<Biquad, 2> cut_;
    float appliedDb_ = 0.0f;
    bool idle_ = true;
    std::atomic<float> reductionOut_ { 0.0f };
};

} // namespace titv::dsp
