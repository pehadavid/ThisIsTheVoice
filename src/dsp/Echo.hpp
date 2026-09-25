// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Biquad.hpp"
#include "DelayLine.hpp"
#include "Smoother.hpp"

#include <array>
#include <cstdint>

namespace titv::dsp {

// ECHO: tempo-synced delay fed by a send.
//
// Two lines form a ping-pong pair; Bounce morphs between a mono centred delay
// (left line only, heard on both sides) and repeats alternating left and right.
// Each tap goes through the Lo-Fi telephone filter, dosed by the knob, before being
// heard and fed back, so every repeat is filtered once more. The loop also has a DC
// blocker and a soft clip; its gain (Repeats) stays below 0.95.
//
// A change of delay time reads from a second head and crossfades to it, so the
// read position never jumps. The engine applies the return, ducking and section
// switch; Send at zero only stops new input and lets the tail ring.
class Echo {
public:
    static constexpr double kMaxDelaySeconds = 6.0;
    static constexpr float kTimeFadeSeconds = 0.050f;
    static constexpr double kLofiLowHz = 500.0, kLofiHighHz = 2800.0;
    static constexpr float kSleepThreshold = 1e-6f; // about -120 dBFS

    void prepare(double sampleRate);
    void reset() noexcept;

    void setSend(float gain) noexcept { send_.setTarget(gain); }
    void setFeedback(float gain) noexcept { feedback_.setTarget(std::min(gain, 0.95f)); }
    void setLofi(float amount01) noexcept { lofi_.setTarget(amount01); }
    void setBounce(bool on) noexcept { bounce_.setTarget(on ? 1.0f : 0.0f); }
    void setDelaySeconds(double seconds) noexcept;

    // Reads the mono send input and writes the stereo return (overwriting outL/outR).
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames) noexcept;

    bool isSleeping() const noexcept { return sleeping_; }
    float currentDelaySamples() const noexcept { return delay_; }

private:
    struct Lofi {
        Biquad hp, lp;
        float process(float x, float amount) noexcept
        {
            const float band = lp.process(hp.process(x));
            return x + amount * (band - x);
        }
    };

    float readTap(const DelayLine& line) const noexcept;

    double sampleRate_ = 48000.0;
    Smoother send_, feedback_, lofi_, bounce_;
    std::array<DelayLine, 2> lines_;
    std::array<Lofi, 2> lofiFilters_;
    std::array<float, 2> dcIn_ {}, dcOut_ {};
    float dcCoeff_ = 0.999f;

    // Delay time: current head, next head and the crossfade between them.
    float delay_ = 1.0f, nextDelay_ = 1.0f, pendingDelay_ = 1.0f;
    float fade_ = 0.0f, fadeStep_ = 0.0f;
    bool fading_ = false;

    bool sleeping_ = true;
    uint32_t quietSamples_ = 0;
};

} // namespace titv::dsp
