// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Biquad.hpp"
#include "Envelope.hpp"
#include "Smoother.hpp"

#include <array>
#include <cstdint>

namespace titv::dsp {

// TONE section: a fixed, subtle tonal-colour curve followed by four bands.
// Body is a low shelf, Mid and Presence are bells, and Air is a high shelf whose
// boost backs off while a high-band detector hears harshness or sibilance.
//
// Coefficients are updated at control rate, every kControlInterval samples, from
// gains smoothed in dB. Both channels share the same gains (linked detection).
class ToneEq {
public:
    static constexpr uint32_t kControlInterval = 16;

    struct Band {
        double freq, q;
    };
    static constexpr Band kBody { 180.0, 0.707 };
    static constexpr Band kMid { 1200.0, 0.9 };
    static constexpr Band kPresence { 4500.0, 0.8 };
    static constexpr Band kAir { 12000.0, 0.707 };

    // Tonal colour: a slight dip in the boxy low mids, a touch of presence and an
    // open top. Fixed; titv_measure prints its response.
    struct ColorBand {
        enum Type { Bell, HighShelf } type;
        double freq, q, gainDb;
    };
    static constexpr std::array<ColorBand, 3> kColorCurve { {
        { ColorBand::Bell, 280.0, 0.8, -1.5 },
        { ColorBand::Bell, 2800.0, 0.7, 1.0 },
        { ColorBand::HighShelf, 9000.0, 0.6, 1.5 },
    } };

    // Dynamic Air: the high-band share of the level (0..1) above which the boost is
    // reduced, the share at which the reduction is complete, and how much of the
    // boost is removed at most.
    static constexpr double kAirDetectorHz = 5000.0;
    static constexpr float kHarshnessStart = 0.25f;
    static constexpr float kHarshnessFull = 0.60f;
    static constexpr float kAirMaxReduction = 0.7f;

    void prepare(double sampleRate);
    void reset() noexcept;

    void setGains(float bodyDb, float midDb, float presenceDb, float airDb) noexcept;
    // Diagnostic option: process without the tonal-colour curve.
    void setTonalColor(bool enabled) noexcept { tonalColor_ = enabled; }

    void process(float* left, float* right, uint32_t frames) noexcept;

    // Air gain actually applied, after the dynamic reduction.
    float airAppliedDb() const noexcept { return airApplied_; }

    static BiquadCoeffs colorCoeffs(const ColorBand& b, double sampleRate) noexcept;

private:
    void updateCoefficients() noexcept;

    double sampleRate_ = 48000.0;
    bool tonalColor_ = true;
    uint32_t countdown_ = 0;

    std::array<std::array<Biquad, 2>, kColorCurve.size()> color_;
    std::array<std::array<Biquad, 2>, 4> bands_;
    std::array<Smoother, 4> gains_;
    std::array<float, 4> appliedDb_ {};
    std::array<bool, 4> bandActive_ {}; // a band at exactly 0 dB is skipped

    Biquad airDetector_;
    EnvelopeFollower highEnv_, fullEnv_;
    EnvelopeFollower airReduction_; // in dB of reduction, rises fast and falls slowly
    float airApplied_ = 0.0f;
};

} // namespace titv::dsp
