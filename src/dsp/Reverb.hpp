// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Biquad.hpp"
#include "DelayLine.hpp"

#include <array>
#include <cstdint>

namespace titv::dsp {

// One SPACE reverb engine: a feedback delay network with a Householder
// matrix, per-line decay gains set from RT60, one-pole damping in the loop, optional
// modulated reads, an input predelay and allpass diffusers, early-reflection taps
// (Room) and a return EQ. The four engines are instances with different designs.
//
// It processes a mono send into a stereo return, and sleeps once its input is
// silent and its output has decayed below -120 dBFS.
class Reverb {
public:
    static constexpr uint32_t kMaxLines = 8;
    static constexpr uint32_t kMaxDiffusers = 4;
    static constexpr uint32_t kMaxTaps = 12;
    static constexpr float kSleepThreshold = 1e-6f;

    struct Tap {
        float ms, gain;
        bool left;
    };

    struct Design {
        const char* name;
        uint32_t lines;
        std::array<float, kMaxLines> delaysMs;
        float rt60Seconds;
        float dampingHz;         // one-pole low-pass in the loop
        float modDepthMs, modRateHz;
        uint32_t diffusers;
        std::array<float, kMaxDiffusers> diffuserMs;
        float diffusion;          // allpass gain
        float predelayMs;
        uint32_t taps;            // early reflections, read from the predelay line
        std::array<Tap, kMaxTaps> earlyTaps;
        float earlyLevel, lateLevel;
        float returnHighPassHz, returnLowPassHz;
        float outputGain;
    };

    static const Design kRoom, kPlate, kHall, kAmbient;

    explicit Reverb(const Design& design) noexcept : design_(design) {}

    void prepare(double sampleRate);
    void reset() noexcept;

    // Adds the stereo return of the mono input to outL / outR.
    void process(const float* in, float* outL, float* outR, uint32_t frames) noexcept;

    const Design& design() const noexcept { return design_; }
    bool isSleeping() const noexcept { return sleeping_; }

private:
    struct Allpass {
        DelayLine line;
        uint32_t delay = 1;
        float process(float x, float g) noexcept
        {
            const float d = line.read(delay - 1);
            const float w = x + g * d;
            line.push(w);
            return d - g * w;
        }
    };

    const Design& design_;
    double sampleRate_ = 48000.0;

    DelayLine predelay_;
    uint32_t predelaySamples_ = 0;
    std::array<uint32_t, kMaxTaps> tapSamples_ {};
    std::array<Allpass, kMaxDiffusers> diffusers_;

    std::array<DelayLine, kMaxLines> lines_;
    std::array<float, kMaxLines> lineDelay_ {}, lineGain_ {}, dampState_ {};
    float dampCoeff_ = 0.0f;
    float modDepth_ = 0.0f;
    Lfo lfo_;

    std::array<Biquad, 2> returnHp_, returnLp_;

    bool sleeping_ = true;
    uint32_t quietSamples_ = 0;
    uint32_t longestDelay_ = 0;
};

} // namespace titv::dsp
