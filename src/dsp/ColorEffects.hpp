// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Biquad.hpp"
#include "DelayLine.hpp"
#include "Smoother.hpp"

#include <array>
#include <cstdint>

namespace titv::dsp {

// COLOR creative effects. Each one leaves the signal exactly untouched
// while its knob is at 0 %, and then idles: filters stop, while delay lines keep
// receiving the input and LFOs keep turning, so that turning the knob up is seamless.

// Saturate: parallel harmonic saturation. A high-passed copy is driven into a tanh
// curve with first-order antiderivative anti-aliasing (ADAA), which adds no declared
// latency. Only the difference between the saturated and the clean copy (harmonics
// and peak rounding) is low-passed and added to the signal, so low levels stay clean.
class Saturator {
public:
    static constexpr double kPreHighPassHz = 150.0;
    static constexpr double kPostLowPassHz = 9000.0;
    static constexpr float kMaxDriveDb = 24.0f;
    static constexpr float kMakeup = 1.35f; // keeps the level comparable at full drive

    void prepare(double sampleRate);
    void reset() noexcept;
    void setAmount(float amount01) noexcept { amount_.setTarget(amount01); }
    void process(float* left, float* right, uint32_t frames) noexcept;

    // tanh antiderivative, log(cosh(u)), in a form that does not overflow.
    static double logCosh(double u) noexcept;

private:
    struct Channel {
        Biquad pre, post;
        double previous = 0.0;  // previous driven sample
        double previousF = 0.0; // its antiderivative
        float previousClean = 0.0f;
        float process(float x, float drive) noexcept;
    };
    Smoother amount_;
    float driveAmount_ = -1.0f, drive_ = 1.0f;
    bool idle_ = true;
    std::array<Channel, 2> ch_;
};

// Radio: telephone band (4th-order high-pass and low-pass around 350 Hz-3.4 kHz) with
// a mid bump, crossfaded in with the knob.
class Radio {
public:
    static constexpr double kLowHz = 350.0, kHighHz = 3400.0, kBumpHz = 1700.0;
    static constexpr double kBumpDb = 4.0;
    static constexpr float kMakeup = 1.9f; // telephone band keeps the voice level comparable

    void prepare(double sampleRate);
    void reset() noexcept;
    void setAmount(float amount01) noexcept { amount_.setTarget(amount01); }
    void process(float* left, float* right, uint32_t frames) noexcept;

private:
    Smoother amount_;
    bool idle_ = true;
    std::array<std::array<Biquad, 5>, 2> filters_;
};

// Knob law of the modulation effects: square root, so that low settings are already
// clearly audible (half the effect at 25 %).
inline float modulationAmount(float knob01) noexcept { return std::sqrt(std::max(knob01, 0.0f)); }

// Double: two copies standing in for a second take: 21 and 33 ms late, with a pitch
// drift of about 10 cents from two non-harmonic LFOs each (so it never sounds
// periodic), slightly duller than the voice. They are added mostly as a side signal
// (+ left, - right), so the mono sum stays close to the dry voice, plus a centred part
// for thickness in mono. The output is normalised so the knob changes the width, not
// the loudness.
class Doubler {
public:
    static constexpr float kSideLevel = 1.6f;
    static constexpr float kCentreShare = 0.55f;
    static constexpr double kCopyLowPassHz = 7000.0;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setAmount(float amount01) noexcept { amount_.setTarget(amount01); }
    void process(float* left, float* right, uint32_t frames) noexcept;

private:
    double sampleRate_ = 48000.0;
    Smoother amount_;
    bool idle_ = true;
    Biquad highPass_, lowPass_;
    DelayLine line_;
    std::array<Lfo, 4> lfos_; // two per copy
};

// Chorus: per channel, two voices on fractional delays modulated by one phase-
// continuous LFO, in quadrature between the channels; the right voices sit on longer
// delays than the left ones, for width. At 100 % the voices are louder
// than the dry signal (a generous, lush chorus); the output is normalised to keep the
// loudness steady.
class Chorus {
public:
    static constexpr double kRateHz = 0.9;
    static constexpr float kDepthMs = 4.5f;
    static constexpr float kWetLevel = 1.4f;
    static constexpr float kNormalisation = 0.5f;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setAmount(float amount01) noexcept { amount_.setTarget(amount01); }
    void process(float* left, float* right, uint32_t frames) noexcept;

private:
    double sampleRate_ = 48000.0;
    Smoother amount_;
    std::array<DelayLine, 2> lines_;
    Lfo lfo_;
};

} // namespace titv::dsp
