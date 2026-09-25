// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Math.hpp"

namespace titv::dsp {

// Detector common to every module: e[n] = a * e[n-1] + (1 - a) * x[n],
// with a = exp(-1 / (tau * fs)) and separate time constants for rise and fall.
// Feed |x| for an amplitude envelope or x^2 for a power envelope.
class EnvelopeFollower {
public:
    void prepare(float attackSeconds, float releaseSeconds, double rate) noexcept
    {
        attack_ = onePoleCoeff(attackSeconds, rate);
        release_ = onePoleCoeff(releaseSeconds, rate);
    }

    void reset(float value = 0.0f) noexcept { value_ = value; }
    float value() const noexcept { return value_; }

    float process(float x) noexcept
    {
        const float a = x > value_ ? attack_ : release_;
        value_ = a * value_ + (1.0f - a) * x;
        return value_;
    }

private:
    float attack_ = 0.0f, release_ = 0.0f;
    float value_ = 0.0f;
};

// Static compression curve with a quadratic soft knee. Returns the gain change in dB
// (<= 0) for an input level in dB.
inline float compressorGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;
    if (2.0f * over <= -kneeDb)
        return 0.0f;
    if (kneeDb > 0.0f && 2.0f * over < kneeDb) {
        const float x = over + kneeDb / 2.0f;
        return slope * x * x / (2.0f * kneeDb);
    }
    return slope * over;
}

// Continuous soft clipper: identity up to the knee, then a tanh curve that tends
// towards +-1 without reaching it. The slope is continuous at the knee.
inline float softClip(float x, float knee) noexcept
{
    const float a = std::fabs(x);
    if (a <= knee)
        return x;
    const float room = 1.0f - knee;
    const float y = knee + room * std::tanh((a - knee) / room);
    return x < 0.0f ? -y : y;
}

inline float levelDb(float amplitude) noexcept { return gainToDb(amplitude, -150.0f); }
inline float powerDb(float power) noexcept { return power > 1e-15f ? 10.0f * std::log10(power) : -150.0f; }

} // namespace titv::dsp
