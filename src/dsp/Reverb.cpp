// SPDX-License-Identifier: GPL-3.0-or-later
#include "Reverb.hpp"

#include <algorithm>
#include <cmath>

namespace titv::dsp {

// Designs created for the project. Delay lengths are spread without common factors
// so that the modes of the lines do not line up. Output gains put each return about
// 6 dB under the dry voice at 100 % send (see titv_measure).
const Reverb::Design Reverb::kRoom {
    "Room", 4,
    { 23.3f, 29.1f, 35.7f, 41.9f },
    0.5f, 5000.0f, 0.0f, 0.0f,
    2, { 2.3f, 4.1f }, 0.6f,
    8.0f,
    12,
    { { { 7.1f, 0.80f, true }, { 9.7f, 0.70f, false }, { 13.3f, 0.60f, true }, { 17.9f, 0.55f, false },
        { 21.4f, 0.50f, false }, { 26.3f, 0.45f, true }, { 31.9f, 0.38f, true }, { 37.2f, 0.33f, false },
        { 44.6f, 0.28f, true }, { 52.1f, 0.22f, false }, { 59.5f, 0.18f, true }, { 67.7f, 0.14f, false } } },
    0.45f, 0.5f,
    120.0f, 9000.0f,
    0.77f,
};

const Reverb::Design Reverb::kPlate {
    "Plate", 8,
    { 10.3f, 12.9f, 15.7f, 18.1f, 21.3f, 24.7f, 28.1f, 31.3f },
    1.8f, 9000.0f, 0.3f, 0.7f,
    4, { 1.9f, 3.1f, 5.3f, 7.9f }, 0.7f,
    0.0f,
    0, {},
    0.0f, 1.0f,
    150.0f, 12000.0f,
    0.74f,
};

const Reverb::Design Reverb::kHall {
    "Hall", 8,
    { 31.1f, 37.9f, 43.3f, 49.7f, 56.3f, 61.9f, 68.9f, 75.1f },
    2.8f, 6000.0f, 0.5f, 0.35f,
    4, { 4.3f, 6.7f, 9.9f, 13.1f }, 0.65f,
    24.0f,
    0, {},
    0.0f, 1.0f,
    120.0f, 9000.0f,
    0.78f,
};

const Reverb::Design Reverb::kAmbient {
    "Ambient", 8,
    { 53.9f, 64.1f, 72.7f, 83.3f, 91.9f, 101.7f, 113.3f, 127.1f },
    6.0f, 4500.0f, 1.2f, 0.2f,
    4, { 5.9f, 8.3f, 11.7f, 15.9f }, 0.65f,
    40.0f,
    0, {},
    0.0f, 1.0f,
    250.0f, 7000.0f,
    0.93f,
};

namespace {
uint32_t samples(float ms, double sampleRate)
{
    return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(ms * 0.001 * sampleRate)));
}
} // namespace

void Reverb::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    const Design& d = design_;

    predelaySamples_ = samples(d.predelayMs, sampleRate);
    float longestTap = 0.0f;
    for (uint32_t i = 0; i < d.taps; ++i) {
        tapSamples_[i] = samples(d.earlyTaps[i].ms, sampleRate);
        longestTap = std::max(longestTap, d.earlyTaps[i].ms);
    }
    predelay_.prepare(samples(std::max(d.predelayMs, longestTap), sampleRate) + 2);

    for (uint32_t i = 0; i < d.diffusers; ++i) {
        diffusers_[i].delay = samples(d.diffuserMs[i], sampleRate);
        diffusers_[i].line.prepare(diffusers_[i].delay + 2);
    }

    modDepth_ = static_cast<float>(d.modDepthMs * 0.001 * sampleRate);
    dampCoeff_ = static_cast<float>(std::exp(-2.0 * kPi * d.dampingHz / sampleRate));
    // RT60 is the mid-band decay: the loop gain makes up for the damping filter's small
    // loss at 1 kHz, so only the highs decay faster than designed.
    const double w = 2.0 * kPi * 1000.0 / sampleRate;
    const double dc = dampCoeff_;
    const double dampAt1k = (1.0 - dc) / std::sqrt(1.0 - 2.0 * dc * std::cos(w) + dc * dc);
    longestDelay_ = predelaySamples_;
    for (uint32_t i = 0; i < d.lines; ++i) {
        const uint32_t len = samples(d.delaysMs[i], sampleRate);
        lineDelay_[i] = static_cast<float>(len);
        lines_[i].prepare(len + static_cast<uint32_t>(modDepth_) + 4);
        // Decay of 60 dB over RT60: gain per pass of a line of length len.
        lineGain_[i] = static_cast<float>(std::pow(10.0, -3.0 * len / (d.rt60Seconds * sampleRate)) / dampAt1k);
        longestDelay_ = std::max(longestDelay_, predelaySamples_ + len);
    }

    lfo_.prepare(sampleRate);
    lfo_.setRate(d.modRateHz);

    for (int c = 0; c < 2; ++c) {
        returnHp_[c].setCoeffs(BiquadCoeffs::highPass(d.returnHighPassHz, 0.707, sampleRate));
        returnLp_[c].setCoeffs(BiquadCoeffs::lowPass(d.returnLowPassHz, 0.707, sampleRate));
    }
    reset();
}

void Reverb::reset() noexcept
{
    predelay_.reset();
    for (Allpass& a : diffusers_)
        a.line.reset();
    for (DelayLine& l : lines_)
        l.reset();
    dampState_.fill(0.0f);
    for (int c = 0; c < 2; ++c) {
        returnHp_[c].reset();
        returnLp_[c].reset();
    }
    lfo_.setPhase(0.0);
    sleeping_ = true;
    quietSamples_ = 0;
}

void Reverb::process(const float* in, float* outL, float* outR, uint32_t frames) noexcept
{
    bool silentInput = true;
    for (uint32_t i = 0; i < frames && silentInput; ++i)
        silentInput = in[i] == 0.0f;
    if (sleeping_ && silentInput)
        return;
    sleeping_ = false;

    const Design& d = design_;
    const uint32_t N = d.lines;
    const float householder = 2.0f / static_cast<float>(N);
    const float inputScale = 1.0f / std::sqrt(static_cast<float>(N));
    const float outputScale = d.outputGain / std::sqrt(0.5f * static_cast<float>(N));
    const bool modulated = modDepth_ > 0.0f;

    float loudest = 0.0f;
    for (uint32_t n = 0; n < frames; ++n) {
        const float x = in[n];
        predelay_.push(x);

        float earlyL = 0.0f, earlyR = 0.0f;
        for (uint32_t t = 0; t < d.taps; ++t) {
            const float v = d.earlyTaps[t].gain * predelay_.read(tapSamples_[t]);
            (d.earlyTaps[t].left ? earlyL : earlyR) += v;
        }

        float late = predelay_.read(predelaySamples_);
        for (uint32_t i = 0; i < d.diffusers; ++i)
            late = diffusers_[i].process(late, d.diffusion);

        // Read the lines; four of them are modulated in quadrature by one LFO.
        std::array<float, kMaxLines> s;
        float sn = 0.0f, cs = 0.0f;
        if (modulated) {
            const double theta = 2.0 * kPi * lfo_.phase();
            lfo_.next();
            sn = static_cast<float>(std::sin(theta));
            cs = static_cast<float>(std::cos(theta));
        }
        for (uint32_t i = 0; i < N; ++i) {
            if (modulated && i < 4) {
                const float m = (i == 0 ? sn : i == 1 ? cs : i == 2 ? -sn : -cs) * modDepth_;
                s[i] = lines_[i].readCubic(lineDelay_[i] - 1.0f + m);
            } else {
                s[i] = lines_[i].read(static_cast<uint32_t>(lineDelay_[i]) - 1);
            }
        }

        // Damping and decay, then the Householder feedback matrix.
        float sum = 0.0f;
        std::array<float, kMaxLines> u;
        for (uint32_t i = 0; i < N; ++i) {
            dampState_[i] = s[i] + dampCoeff_ * (dampState_[i] - s[i]);
            u[i] = lineGain_[i] * dampState_[i];
            sum += u[i];
        }
        const float injected = late * inputScale;
        for (uint32_t i = 0; i < N; ++i)
            lines_[i].push(u[i] - householder * sum + ((i & 1) ? -injected : injected));

        // Left and right take different lines, for a wide, decorrelated return.
        float lateL = 0.0f, lateR = 0.0f;
        for (uint32_t i = 0; i < N; i += 2) {
            const float sign = (i / 2) % 2 == 0 ? 1.0f : -1.0f;
            lateL += sign * s[i];
            lateR += sign * s[i + 1];
        }

        float l = d.earlyLevel * earlyL + d.lateLevel * lateL * outputScale;
        float r = d.earlyLevel * earlyR + d.lateLevel * lateR * outputScale;
        l = returnLp_[0].process(returnHp_[0].process(l));
        r = returnLp_[1].process(returnHp_[1].process(r));
        outL[n] += l;
        outR[n] += r;
        loudest = std::max(loudest, std::max(std::fabs(l), std::fabs(r)));
    }

    if (silentInput && loudest < kSleepThreshold) {
        quietSamples_ += frames;
        if (quietSamples_ > longestDelay_ + frames)
            sleeping_ = true;
    } else {
        quietSamples_ = 0;
    }
}

} // namespace titv::dsp
