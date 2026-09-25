// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

namespace titv::dsp {

inline constexpr double kPi = 3.14159265358979323846;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

inline float gainToDb(float gain, float floorDb = -120.0f) noexcept
{
    return gain > 0.0f ? std::fmax(20.0f * std::log10(gain), floorDb) : floorDb;
}

// One-pole coefficient for a time constant independent of the sample rate:
// a = exp(-1 / (tau * fs)), used as y[n] = a * y[n-1] + (1 - a) * x[n].
inline float onePoleCoeff(float tauSeconds, double sampleRate) noexcept
{
    if (tauSeconds <= 0.0f || sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (static_cast<double>(tauSeconds) * sampleRate)));
}

// Non-finite samples (NaN, inf) are replaced by silence before they reach recursive state.
inline float sanitize(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }

} // namespace titv::dsp
