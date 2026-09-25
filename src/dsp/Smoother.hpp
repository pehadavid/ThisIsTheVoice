// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Math.hpp"

#include <cstdint>

namespace titv::dsp {

// One-pole parameter smoother. Snaps to the target once the remaining distance is
// negligible, so a settled smoother returns the exact target value.
class Smoother {
public:
    void prepare(float tauSeconds, double sampleRate) noexcept { a_ = onePoleCoeff(tauSeconds, sampleRate); }
    void setTarget(float target) noexcept { target_ = target; }
    void snap(float value) noexcept { target_ = current_ = value; }

    float target() const noexcept { return target_; }
    bool isSettled() const noexcept { return current_ == target_; }

    float next() noexcept
    {
        current_ = a_ * current_ + (1.0f - a_) * target_;
        if (std::fabs(current_ - target_) < 1e-6f)
            current_ = target_;
        return current_;
    }

private:
    float a_ = 0.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
};

// Linear crossfade between 0 and 1 over a fixed duration, used for bypasses and
// on/off switches so that a toggle never produces a step in the output.
class Crossfade {
public:
    void prepare(float seconds, double sampleRate) noexcept
    {
        const double samples = seconds * sampleRate;
        step_ = samples > 1.0 ? static_cast<float>(1.0 / samples) : 1.0f;
    }

    void setOn(bool on) noexcept { target_ = on ? 1.0f : 0.0f; }
    void snap(bool on) noexcept { target_ = value_ = on ? 1.0f : 0.0f; }

    float value() const noexcept { return value_; }
    bool isSettled() const noexcept { return value_ == target_; }

    // Same as calling next() frames times.
    void advance(uint32_t frames) noexcept
    {
        for (uint32_t i = 0; i < frames && value_ != target_; ++i)
            next();
    }

    float next() noexcept
    {
        if (value_ < target_)
            value_ = value_ + step_ >= target_ ? target_ : value_ + step_;
        else if (value_ > target_)
            value_ = value_ - step_ <= target_ ? target_ : value_ - step_;
        return value_;
    }

private:
    float step_ = 1.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace titv::dsp
