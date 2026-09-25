// SPDX-License-Identifier: GPL-3.0-or-later
#include "Echo.hpp"

#include "Envelope.hpp"

#include <algorithm>
#include <cmath>

namespace titv::dsp {

namespace {
constexpr float kParamSmoothingSeconds = 0.030f;
constexpr float kBounceSmoothingSeconds = 0.050f;
constexpr float kLoopClipKnee = 0.9f;
} // namespace

void Echo::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    for (Smoother* s : { &send_, &feedback_, &lofi_ })
        s->prepare(kParamSmoothingSeconds, sampleRate);
    bounce_.prepare(kBounceSmoothingSeconds, sampleRate);
    for (DelayLine& l : lines_)
        l.prepare(static_cast<uint32_t>(kMaxDelaySeconds * sampleRate) + 8);
    for (Lofi& f : lofiFilters_) {
        f.hp.setCoeffs(BiquadCoeffs::highPass(kLofiLowHz, 0.707, sampleRate));
        f.lp.setCoeffs(BiquadCoeffs::lowPass(kLofiHighHz, 0.707, sampleRate));
    }
    dcCoeff_ = onePoleCoeff(1.0f / (2.0f * static_cast<float>(kPi) * 10.0f), sampleRate); // ~10 Hz
    fadeStep_ = 1.0f / std::max(1.0f, kTimeFadeSeconds * static_cast<float>(sampleRate));
    reset();
}

void Echo::reset() noexcept
{
    for (Smoother* s : { &send_, &feedback_, &lofi_, &bounce_ })
        s->snap(s->target());
    for (DelayLine& l : lines_)
        l.reset();
    for (Lofi& f : lofiFilters_) {
        f.hp.reset();
        f.lp.reset();
    }
    dcIn_.fill(0.0f);
    dcOut_.fill(0.0f);
    delay_ = nextDelay_ = pendingDelay_;
    fading_ = false;
    fade_ = 0.0f;
    sleeping_ = true;
    quietSamples_ = 0;
}

void Echo::setDelaySeconds(double seconds) noexcept
{
    const double maxSamples = static_cast<double>(lines_[0].maxDelay() - 4);
    pendingDelay_ = static_cast<float>(std::clamp(seconds * sampleRate_, 1.0, maxSamples));
}

void Echo::process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames) noexcept
{
    // Sleep once the send is silent and the loop has decayed below -120 dBFS for
    // longer than the delay: only zeros are written then, which keeps the buffer
    // consistent at a fraction of the cost.
    bool silentInput = send_.isSettled() && send_.target() == 0.0f;
    if (!silentInput) {
        silentInput = true;
        for (uint32_t i = 0; i < frames && silentInput; ++i)
            silentInput = inL[i] == 0.0f && inR[i] == 0.0f;
    }
    if (sleeping_ && silentInput) {
        for (uint32_t i = 0; i < frames; ++i) {
            lines_[0].push(0.0f);
            lines_[1].push(0.0f);
        }
        std::fill(outL, outL + frames, 0.0f);
        std::fill(outR, outR + frames, 0.0f);
        delay_ = nextDelay_ = pendingDelay_;
        return;
    }
    sleeping_ = false;

    float loudest = 0.0f;
    for (uint32_t n = 0; n < frames; ++n) {
        if (!fading_ && pendingDelay_ != delay_) {
            nextDelay_ = pendingDelay_;
            fading_ = true;
            fade_ = 0.0f;
        }

        const float send = send_.next();
        const float fb = feedback_.next();
        const float lofi = lofi_.next();
        const float b = bounce_.next();

        // Taps, then Lo-Fi, heard and fed back.
        const float tapL = lofiFilters_[0].process(readTap(lines_[0]), lofi);
        const float tapR = lofiFilters_[1].process(readTap(lines_[1]), lofi);
        if (fading_) {
            fade_ += fadeStep_;
            if (fade_ >= 1.0f) {
                fading_ = false;
                delay_ = nextDelay_;
            }
        }

        const float input = send * 0.5f * (inL[n] + inR[n]);
        float feedL = input + fb * ((1.0f - b) * tapL + b * tapR);
        float feedR = fb * b * tapL;

        // DC blocker and soft clip keep the loop bounded under sustained input.
        for (int c = 0; c < 2; ++c) {
            float& x = c == 0 ? feedL : feedR;
            const float y = x - dcIn_[c] + dcCoeff_ * dcOut_[c];
            dcIn_[c] = x;
            dcOut_[c] = y;
            x = softClip(y, kLoopClipKnee);
        }
        lines_[0].push(feedL);
        lines_[1].push(feedR);

        outL[n] = tapL;
        outR[n] = (1.0f - b) * tapL + b * tapR;
        loudest = std::max(loudest, std::max(std::fabs(feedL), std::fabs(feedR)));
    }

    if (silentInput && loudest < kSleepThreshold) {
        quietSamples_ += frames;
        if (quietSamples_ > static_cast<uint32_t>(delay_) + frames)
            sleeping_ = true;
    } else {
        quietSamples_ = 0;
    }
}

float Echo::readTap(const DelayLine& line) const noexcept
{
    // Read before this sample's push: a delay of D samples is D - 1 behind the last one.
    const float current = line.readCubic(delay_ - 1.0f);
    if (!fading_)
        return current;
    // Linear crossfade between the two heads.
    return current + fade_ * (line.readCubic(nextDelay_ - 1.0f) - current);
}

} // namespace titv::dsp
