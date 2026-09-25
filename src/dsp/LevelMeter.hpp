// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Math.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace titv::dsp {

// Level meter fed by the audio thread and read by the UI thread.
// RMS is a one-pole average of power; peak has instant attack and exponential release.
// The clip indicator latches until the reader clears it.
class LevelMeter {
public:
    static constexpr float kClipThreshold = 1.0f;

    void prepare(double sampleRate) noexcept
    {
        rmsCoeff_ = onePoleCoeff(0.300f, sampleRate);
        peakCoeff_ = onePoleCoeff(0.500f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        power_ = peak_ = 0.0f;
        rms_.store(0.0f, std::memory_order_relaxed);
        peakOut_.store(0.0f, std::memory_order_relaxed);
    }

    // Audio thread: accumulates one block of up to two channels.
    void process(const float* left, const float* right, uint32_t frames) noexcept
    {
        bool clipped = false;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = left[i];
            const float r = right != nullptr ? right[i] : l;
            const float p = 0.5f * (l * l + r * r);
            power_ = rmsCoeff_ * power_ + (1.0f - rmsCoeff_) * p;
            const float a = std::max(std::fabs(l), std::fabs(r));
            peak_ = a > peak_ ? a : peakCoeff_ * peak_;
            clipped |= a >= kClipThreshold;
        }
        if (power_ < 1e-15f)
            power_ = 0.0f;
        if (peak_ < 1e-8f)
            peak_ = 0.0f;
        rms_.store(std::sqrt(power_), std::memory_order_relaxed);
        peakOut_.store(peak_, std::memory_order_relaxed);
        if (clipped)
            clip_.store(true, std::memory_order_relaxed);
    }

    // Any thread.
    float rms() const noexcept { return rms_.load(std::memory_order_relaxed); }
    float peak() const noexcept { return peakOut_.load(std::memory_order_relaxed); }
    bool clipped() const noexcept { return clip_.load(std::memory_order_relaxed); }
    void clearClip() noexcept { clip_.store(false, std::memory_order_relaxed); }

private:
    float rmsCoeff_ = 0.0f, peakCoeff_ = 0.0f;
    float power_ = 0.0f, peak_ = 0.0f;
    std::atomic<float> rms_ { 0.0f };
    std::atomic<float> peakOut_ { 0.0f };
    std::atomic<bool> clip_ { false };
};

} // namespace titv::dsp
