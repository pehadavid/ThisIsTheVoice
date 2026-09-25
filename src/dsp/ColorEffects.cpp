// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorEffects.hpp"

#include <cmath>

namespace titv::dsp {

namespace {
constexpr float kAmountSmoothingSeconds = 0.030f;

double ms(double milliseconds, double sampleRate) { return milliseconds * 0.001 * sampleRate; }
} // namespace

// --- Saturator -------------------------------------------------------------------

double Saturator::logCosh(double u) noexcept
{
    const double a = std::fabs(u);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.69314718055994531;
}

void Saturator::prepare(double sampleRate)
{
    amount_.prepare(kAmountSmoothingSeconds, sampleRate);
    for (Channel& c : ch_) {
        c.pre.setCoeffs(BiquadCoeffs::highPass(kPreHighPassHz, 0.707, sampleRate));
        c.post.setCoeffs(BiquadCoeffs::lowPass(kPostLowPassHz, 0.707, sampleRate));
    }
    reset();
}

void Saturator::reset() noexcept
{
    amount_.snap(amount_.target());
    for (Channel& c : ch_) {
        c.pre.reset();
        c.post.reset();
        c.previous = 0.0;
        c.previousF = 0.0;
        c.previousClean = 0.0f;
    }
}

float Saturator::Channel::process(float x, float drive) noexcept
{
    const float clean = pre.process(x);
    const double u = static_cast<double>(drive) * clean;
    const double du = u - previous;
    // ADAA: average of tanh over the segment between the last two driven samples.
    const double F = logCosh(u);
    const double shaped = std::fabs(du) > 1e-5 ? (F - previousF) / du : std::tanh(0.5 * (u + previous));
    previous = u;
    previousF = F;
    // ADAA delays the signal by half a sample; compare with the clean copy delayed alike.
    const float cleanHalf = 0.5f * (clean + previousClean);
    previousClean = clean;
    return post.process(static_cast<float>(shaped / drive) - cleanHalf);
}

void Saturator::process(float* left, float* right, uint32_t frames) noexcept
{
    if (amount_.isSettled() && amount_.target() == 0.0f) {
        idle_ = true;
        return;
    }
    if (idle_) {
        // Restart the filters from silence: the knob ramps up from 0 anyway.
        idle_ = false;
        for (Channel& c : ch_) {
            c.pre.reset();
            c.post.reset();
            c.previous = c.previousF = 0.0;
            c.previousClean = 0.0f;
        }
    }
    for (uint32_t n = 0; n < frames; ++n) {
        const float a = amount_.next();
        if (a != driveAmount_) {
            driveAmount_ = a;
            drive_ = dbToGain(kMaxDriveDb * a);
        }
        const float drive = drive_;
        const float dl = ch_[0].process(left[n], drive);
        const float dr = ch_[1].process(right[n], drive);
        if (a > 0.0f) {
            left[n] += a * kMakeup * dl;
            right[n] += a * kMakeup * dr;
        }
    }
}

// --- Radio -----------------------------------------------------------------------

void Radio::prepare(double sampleRate)
{
    amount_.prepare(kAmountSmoothingSeconds, sampleRate);
    for (auto& f : filters_) {
        f[0].setCoeffs(BiquadCoeffs::highPass(kLowHz, 0.707, sampleRate));
        f[1].setCoeffs(BiquadCoeffs::highPass(kLowHz, 0.707, sampleRate));
        f[2].setCoeffs(BiquadCoeffs::lowPass(kHighHz, 0.707, sampleRate));
        f[3].setCoeffs(BiquadCoeffs::lowPass(kHighHz, 0.707, sampleRate));
        f[4].setCoeffs(BiquadCoeffs::peaking(kBumpHz, 1.2, kBumpDb, sampleRate));
    }
    reset();
}

void Radio::reset() noexcept
{
    amount_.snap(amount_.target());
    for (auto& f : filters_)
        for (Biquad& b : f)
            b.reset();
}

void Radio::process(float* left, float* right, uint32_t frames) noexcept
{
    if (amount_.isSettled() && amount_.target() == 0.0f) {
        idle_ = true;
        return;
    }
    if (idle_) {
        idle_ = false;
        for (auto& f : filters_)
            for (Biquad& b : f)
                b.reset();
    }
    float* io[2] = { left, right };
    for (uint32_t n = 0; n < frames; ++n) {
        const float a = amount_.next();
        for (int c = 0; c < 2; ++c) {
            float y = io[c][n];
            for (Biquad& b : filters_[c])
                y = b.process(y);
            if (a > 0.0f)
                io[c][n] += a * (kMakeup * y - io[c][n]);
        }
    }
}

// --- Doubler ---------------------------------------------------------------------

void Doubler::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    amount_.prepare(kAmountSmoothingSeconds, sampleRate);
    highPass_.setCoeffs(BiquadCoeffs::highPass(150.0, 0.707, sampleRate));
    lowPass_.setCoeffs(BiquadCoeffs::lowPass(kCopyLowPassHz, 0.707, sampleRate));
    line_.prepare(static_cast<uint32_t>(ms(45.0, sampleRate)));
    constexpr double kRates[4] = { 0.19, 0.53, 0.27, 0.71 };
    for (size_t i = 0; i < lfos_.size(); ++i) {
        lfos_[i].prepare(sampleRate);
        lfos_[i].setRate(kRates[i]);
    }
    reset();
}

void Doubler::reset() noexcept
{
    amount_.snap(amount_.target());
    highPass_.reset();
    lowPass_.reset();
    line_.reset();
    constexpr double kPhases[4] = { 0.0, 0.41, 0.23, 0.67 };
    for (size_t i = 0; i < lfos_.size(); ++i)
        lfos_[i].setPhase(kPhases[i]);
}

void Doubler::process(float* left, float* right, uint32_t frames) noexcept
{
    if (amount_.isSettled() && amount_.target() == 0.0f) {
        idle_ = true;
        for (uint32_t n = 0; n < frames; ++n)
            line_.push(0.5f * (left[n] + right[n]));
        for (Lfo& lfo : lfos_)
            lfo.advance(frames);
        return;
    }
    if (idle_) {
        idle_ = false;
        highPass_.reset();
        lowPass_.reset();
    }
    const float baseA = static_cast<float>(ms(21.0, sampleRate_)), depthA = static_cast<float>(ms(3.5, sampleRate_));
    const float baseB = static_cast<float>(ms(33.0, sampleRate_)), depthB = static_cast<float>(ms(4.0, sampleRate_));
    for (uint32_t n = 0; n < frames; ++n) {
        const float g = modulationAmount(amount_.next());
        line_.push(lowPass_.process(highPass_.process(0.5f * (left[n] + right[n]))));
        const float a = line_.readCubic(baseA + depthA * (0.7f * lfos_[0].next() + 0.3f * lfos_[1].next()));
        const float b = line_.readCubic(baseB + depthB * (0.7f * lfos_[2].next() + 0.3f * lfos_[3].next()));
        if (g > 0.0f) {
            const float side = 0.5f * kSideLevel * g * (a - b);
            const float centre = 0.5f * kCentreShare * g * (a + b);
            // Each channel gains roughly the copies' power: keep its level steady.
            const float norm = 1.0f / std::sqrt(1.0f + 0.45f * g * g);
            left[n] = norm * (left[n] + side + centre);
            right[n] = norm * (right[n] + centre - side);
        }
    }
}

// --- Chorus ----------------------------------------------------------------------

void Chorus::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    amount_.prepare(kAmountSmoothingSeconds, sampleRate);
    for (DelayLine& l : lines_)
        l.prepare(static_cast<uint32_t>(ms(30.0, sampleRate)));
    lfo_.prepare(sampleRate);
    lfo_.setRate(kRateHz);
    reset();
}

void Chorus::reset() noexcept
{
    amount_.snap(amount_.target());
    for (DelayLine& l : lines_)
        l.reset();
    lfo_.setPhase(0.0);
}

void Chorus::process(float* left, float* right, uint32_t frames) noexcept
{
    if (amount_.isSettled() && amount_.target() == 0.0f) {
        for (uint32_t n = 0; n < frames; ++n) {
            lines_[0].push(left[n]);
            lines_[1].push(right[n]);
        }
        lfo_.advance(frames);
        return;
    }
    const float base1 = static_cast<float>(ms(9.0, sampleRate_));
    const float base2 = static_cast<float>(ms(14.0, sampleRate_));
    const float base3 = static_cast<float>(ms(11.0, sampleRate_));
    const float base4 = static_cast<float>(ms(16.5, sampleRate_));
    const float depth = static_cast<float>(ms(kDepthMs, sampleRate_));
    for (uint32_t n = 0; n < frames; ++n) {
        const float m = kWetLevel * modulationAmount(amount_.next());
        lines_[0].push(left[n]);
        lines_[1].push(right[n]);
        // One LFO; the four voices read it at phases 0, 1/2 (left) and 1/4, 3/4 (right).
        const double theta = 2.0 * kPi * lfo_.phase();
        lfo_.next();
        const float sn = static_cast<float>(std::sin(theta)), cs = static_cast<float>(std::cos(theta));
        const float wl = 0.5f * (lines_[0].readCubic(base1 + depth * sn) + lines_[0].readCubic(base2 - depth * sn));
        const float wr = 0.5f * (lines_[1].readCubic(base3 + depth * cs) + lines_[1].readCubic(base4 - depth * cs));
        if (m > 0.0f) {
            // The voices are only partly correlated with the dry signal: normalise on power.
            const float norm = 1.0f / std::sqrt(1.0f + kNormalisation * m * m);
            left[n] = norm * (left[n] + m * wl);
            right[n] = norm * (right[n] + m * wr);
        }
    }
}

} // namespace titv::dsp
