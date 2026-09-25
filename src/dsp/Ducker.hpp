// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Envelope.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace titv::dsp {

// Voice-presence detector driving the ducking of the ECHO and SPACE returns
//. A fast level follower on the processed voice, before the effects,
// maps to a presence from 0 to 1 over kFloorDb..kFullDb; the presence itself then
// rises in 10 ms and falls in 300 ms, so the returns dip while the voice is present
// and come back between phrases.
class Ducker {
public:
    static constexpr float kLevelAttackSeconds = 0.005f;
    static constexpr float kLevelReleaseSeconds = 0.050f;
    static constexpr float kAttackSeconds = 0.010f;
    static constexpr float kReleaseSeconds = 0.300f;
    static constexpr float kFloorDb = -50.0f;
    static constexpr float kFullDb = -30.0f;
    static constexpr float kEchoDepthDb = 10.0f;
    static constexpr float kSpaceDepthDb = 6.0f;
    static constexpr uint32_t kControlInterval = 16;

    void prepare(double sampleRate) noexcept
    {
        level_.prepare(kLevelAttackSeconds, kLevelReleaseSeconds, sampleRate);
        presence_.prepare(kAttackSeconds, kReleaseSeconds, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        level_.reset();
        presence_.reset();
        raw_ = 0.0f;
        countdown_ = 0;
        activity_.store(0.0f, std::memory_order_relaxed);
    }

    // Presence (0..1) for one sample of the voice.
    float process(float left, float right) noexcept
    {
        const float level = level_.process(0.5f * std::fabs(left + right));
        // The level-to-presence mapping needs a logarithm: done at control rate.
        if (countdown_ == 0) {
            raw_ = std::clamp((levelDb(level) - kFloorDb) / (kFullDb - kFloorDb), 0.0f, 1.0f);
            countdown_ = kControlInterval;
        }
        --countdown_;
        return presence_.process(raw_);
    }

    static float gain(float presence, float depthDb) noexcept
    {
        return presence > 0.0f ? std::exp(-depthDb * presence * 0.11512925f) : 1.0f; // ln(10) / 20
    }

    // For the editor: last presence value published by the audio thread.
    void publish(float presence) noexcept { activity_.store(presence, std::memory_order_relaxed); }
    float activity() const noexcept { return activity_.load(std::memory_order_relaxed); }

private:
    EnvelopeFollower level_, presence_;
    float raw_ = 0.0f;
    uint32_t countdown_ = 0;
    std::atomic<float> activity_ { 0.0f };
};

} // namespace titv::dsp
