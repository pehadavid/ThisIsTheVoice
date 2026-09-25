// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace titv::dsp {

// Circular delay line with a power-of-two buffer, allocated in prepare().
// Reads take a delay in samples (fractional delays use 4-point Hermite interpolation).
class DelayLine {
public:
    void prepare(uint32_t maxDelaySamples)
    {
        uint32_t size = 4;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer_.assign(size, 0.0f);
        mask_ = size - 1;
        write_ = 0;
    }

    void reset() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }

    uint32_t maxDelay() const noexcept { return mask_ > 4 ? mask_ - 3 : 0; }

    void push(float x) noexcept
    {
        buffer_[write_] = x;
        write_ = (write_ + 1) & mask_;
    }

    // Delay of d samples relative to the last pushed sample (d = 0 returns it).
    float read(uint32_t d) const noexcept { return buffer_[(write_ - 1 - d) & mask_]; }

    float readCubic(float delay) const noexcept
    {
        delay = std::clamp(delay, 1.0f, static_cast<float>(maxDelay()));
        const uint32_t i = static_cast<uint32_t>(delay);
        const float t = delay - static_cast<float>(i);
        const float xm1 = read(i - 1), x0 = read(i), x1 = read(i + 1), x2 = read(i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    std::vector<float> buffer_;
    uint32_t mask_ = 0;
    uint32_t write_ = 0;
};

// Sine LFO with a continuous phase: rate changes never make it jump.
class Lfo {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void setRate(double hz) noexcept { increment_ = hz / sampleRate_; }
    void setPhase(double phase01) noexcept { phase_ = phase01 - std::floor(phase01); }
    double phase() const noexcept { return phase_; }

    // Advances without computing values, for an idle effect.
    void advance(uint32_t frames) noexcept
    {
        phase_ += increment_ * frames;
        phase_ -= std::floor(phase_);
    }

    // Value in [-1, 1] at the given phase offset (0..1), then advances by one sample.
    float next(double offset = 0.0) noexcept
    {
        const float v = static_cast<float>(std::sin(2.0 * kPi * (phase_ + offset)));
        phase_ += increment_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;
        return v;
    }

private:
    double sampleRate_ = 48000.0;
    double increment_ = 0.0;
    double phase_ = 0.0;
};

} // namespace titv::dsp
