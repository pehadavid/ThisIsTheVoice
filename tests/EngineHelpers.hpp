// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/Engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace titv::test {

using Buffer = std::vector<float>;

struct Stereo {
    Buffer l, r;
};

inline Buffer sine(double freq, double sampleRate, size_t frames, float amplitude = 0.5f)
{
    Buffer b(frames);
    for (size_t i = 0; i < frames; ++i)
        b[i] = amplitude * static_cast<float>(std::sin(2.0 * dsp::kPi * freq * static_cast<double>(i) / sampleRate));
    return b;
}

inline float rms(const Buffer& b, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min(to, b.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(b[i]) * b[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)));
}

inline float rmsDb(const Buffer& b, size_t from = 0, size_t to = SIZE_MAX)
{
    return 20.0f * std::log10(std::max(rms(b, from, to), 1e-12f));
}

inline float peak(const Buffer& b, size_t from = 0)
{
    float m = 0.0f;
    for (size_t i = from; i < b.size(); ++i)
        m = std::max(m, std::fabs(b[i]));
    return m;
}

inline void scaleToRmsDb(Buffer& b, float targetDb)
{
    const float g = std::pow(10.0f, (targetDb - rmsDb(b)) / 20.0f);
    for (float& x : b)
        x *= g;
}

inline Buffer whiteNoise(size_t frames, uint32_t seed, float amplitude = 0.5f)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amplitude, amplitude);
    Buffer b(frames);
    for (float& x : b)
        x = d(rng);
    return b;
}

// Filters a buffer through a biquad (tests only).
inline Buffer filtered(const Buffer& in, const dsp::BiquadCoeffs& c)
{
    dsp::Biquad f;
    f.setCoeffs(c);
    Buffer out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = f.process(in[i]);
    return out;
}

// Sibilance stand-in: white noise band-limited to about 5-10 kHz.
inline Buffer sibilantNoise(double fs, size_t frames, uint32_t seed = 7)
{
    Buffer b = filtered(whiteNoise(frames, seed), dsp::BiquadCoeffs::highPass(5000.0, 0.707, fs));
    return filtered(b, dsp::BiquadCoeffs::lowPass(10000.0, 0.707, fs));
}

// Vowel stand-in: harmonics of 180 Hz with a 1/k spectrum up to 3.5 kHz.
inline Buffer vowel(double fs, size_t frames, double f0 = 180.0)
{
    Buffer b(frames, 0.0f);
    for (int k = 1; f0 * k < 3500.0; ++k)
        for (size_t i = 0; i < frames; ++i)
            b[i] += static_cast<float>(std::sin(2.0 * dsp::kPi * f0 * k * static_cast<double>(i) / fs + k) / k);
    return b;
}

// Speech-like test signal: vowels shaped by a 4 Hz syllable envelope, with a short
// sibilant burst every 0.6 s, scaled to the given RMS level.
inline Buffer voiceLike(double fs, double seconds, float rmsDbTarget)
{
    const size_t frames = static_cast<size_t>(fs * seconds);
    Buffer v = vowel(fs, frames);
    const Buffer s = sibilantNoise(fs, frames);
    const size_t burstPeriod = static_cast<size_t>(0.6 * fs), burstLength = static_cast<size_t>(0.08 * fs);
    for (size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / fs;
        const float syllable = static_cast<float>(0.15 + 0.85 * std::pow(std::fabs(std::sin(dsp::kPi * 4.0 * t)), 1.5));
        v[i] *= 0.3f * syllable;
        if (i % burstPeriod < burstLength)
            v[i] = 0.2f * v[i] + 1.2f * s[i];
    }
    scaleToRmsDb(v, rmsDbTarget);
    return v;
}

inline float maxStep(const Buffer& b, size_t from = 1, size_t to = SIZE_MAX)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, b.size()); ++i)
        m = std::max(m, std::fabs(b[i] - b[i - 1]));
    return m;
}

inline size_t argmaxAbs(const Buffer& b)
{
    size_t best = 0;
    for (size_t i = 1; i < b.size(); ++i)
        if (std::fabs(b[i]) > std::fabs(b[best]))
            best = i;
    return best;
}

inline bool allFinite(const Buffer& b)
{
    return std::all_of(b.begin(), b.end(), [](float x) { return std::isfinite(x); });
}

// Renders a stereo (or mono when inR is null) input through the engine in blocks.
// nextBlock returns the size of each block; beforeBlock is called with the start
// frame of each block, to change parameters at known positions.
inline Stereo render(Engine& engine, const Buffer& inL, const Buffer* inR,
                     const std::function<uint32_t()>& nextBlock,
                     const std::function<void(size_t)>& beforeBlock = {})
{
    Stereo out { Buffer(inL.size()), Buffer(inL.size()) };
    for (size_t pos = 0; pos < inL.size();) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(nextBlock(), inL.size() - pos));
        if (beforeBlock)
            beforeBlock(pos);
        const float* ins[2] = { inL.data() + pos, inR != nullptr ? inR->data() + pos : nullptr };
        float* outs[2] = { out.l.data() + pos, out.r.data() + pos };
        engine.process(ins, inR != nullptr ? 2 : 1, outs, 2, n);
        pos += n;
    }
    return out;
}

inline std::function<uint32_t()> fixedBlocks(uint32_t n)
{
    return [n] { return n; };
}

} // namespace titv::test
