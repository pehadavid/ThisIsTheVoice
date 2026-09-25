// SPDX-License-Identifier: GPL-3.0-or-later
#include "ToneEq.hpp"

#include <algorithm>
#include <cmath>

namespace titv::dsp {

namespace {
constexpr float kGainSmoothingSeconds = 0.020f;
constexpr float kDetectorAttackSeconds = 0.001f;
constexpr float kDetectorReleaseSeconds = 0.050f;
constexpr float kAirReductionAttackSeconds = 0.003f;
constexpr float kAirReductionReleaseSeconds = 0.080f;
} // namespace

BiquadCoeffs ToneEq::colorCoeffs(const ColorBand& b, double sampleRate) noexcept
{
    return b.type == ColorBand::HighShelf ? BiquadCoeffs::highShelf(b.freq, b.q, b.gainDb, sampleRate)
                                          : BiquadCoeffs::peaking(b.freq, b.q, b.gainDb, sampleRate);
}

void ToneEq::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    const double controlRate = sampleRate / kControlInterval;

    for (size_t i = 0; i < kColorCurve.size(); ++i)
        for (Biquad& f : color_[i])
            f.setCoeffs(colorCoeffs(kColorCurve[i], sampleRate));

    for (Smoother& g : gains_)
        g.prepare(kGainSmoothingSeconds, controlRate);

    airDetector_.setCoeffs(BiquadCoeffs::highPass(kAirDetectorHz, 0.707, sampleRate));
    highEnv_.prepare(kDetectorAttackSeconds, kDetectorReleaseSeconds, sampleRate);
    fullEnv_.prepare(kDetectorAttackSeconds, kDetectorReleaseSeconds, sampleRate);
    airReduction_.prepare(kAirReductionAttackSeconds, kAirReductionReleaseSeconds, controlRate);
    reset();
}

void ToneEq::reset() noexcept
{
    for (auto& pair : color_)
        for (Biquad& f : pair)
            f.reset();
    for (auto& pair : bands_)
        for (Biquad& f : pair)
            f.reset();
    airDetector_.reset();
    highEnv_.reset();
    fullEnv_.reset();
    airReduction_.reset();
    for (Smoother& g : gains_)
        g.snap(g.target());
    // Force a coefficient update on the next sample.
    appliedDb_.fill(std::nanf(""));
    countdown_ = 0;
}

void ToneEq::setGains(float bodyDb, float midDb, float presenceDb, float airDb) noexcept
{
    gains_[0].setTarget(bodyDb);
    gains_[1].setTarget(midDb);
    gains_[2].setTarget(presenceDb);
    gains_[3].setTarget(airDb);
}

void ToneEq::updateCoefficients() noexcept
{
    std::array<float, 4> db;
    for (size_t i = 0; i < 4; ++i)
        db[i] = gains_[i].next();

    // Harshness: share of the level that sits above the detector frequency.
    const float share = highEnv_.value() / (fullEnv_.value() + 1e-9f);
    const float harsh = std::clamp((share - kHarshnessStart) / (kHarshnessFull - kHarshnessStart), 0.0f, 1.0f);
    const float reduction = airReduction_.process(db[3] > 0.0f ? db[3] * kAirMaxReduction * harsh : 0.0f);
    db[3] = db[3] > 0.0f ? std::max(db[3] - reduction, 0.0f) : db[3];
    airApplied_ = db[3];

    for (size_t i = 0; i < 4; ++i) {
        if (std::fabs(db[i] - appliedDb_[i]) < 1e-4f)
            continue;
        appliedDb_[i] = db[i];
        const bool active = db[i] != 0.0f;
        if (active && !bandActive_[i]) {
            bands_[i][0].reset();
            bands_[i][1].reset();
        }
        bandActive_[i] = active;
        BiquadCoeffs c;
        switch (i) {
        case 0: c = BiquadCoeffs::lowShelf(kBody.freq, kBody.q, db[i], sampleRate_); break;
        case 1: c = BiquadCoeffs::peaking(kMid.freq, kMid.q, db[i], sampleRate_); break;
        case 2: c = BiquadCoeffs::peaking(kPresence.freq, kPresence.q, db[i], sampleRate_); break;
        default: c = BiquadCoeffs::highShelf(kAir.freq, kAir.q, db[i], sampleRate_); break;
        }
        bands_[i][0].setCoeffs(c);
        bands_[i][1].setCoeffs(c);
    }
}

void ToneEq::process(float* left, float* right, uint32_t frames) noexcept
{
    for (uint32_t n = 0; n < frames; ++n) {
        if (countdown_ == 0) {
            updateCoefficients();
            countdown_ = kControlInterval;
        }
        --countdown_;

        float l = left[n], r = right[n];
        const float mid = 0.5f * (l + r);
        highEnv_.process(std::fabs(airDetector_.process(mid)));
        fullEnv_.process(std::fabs(mid));

        if (tonalColor_) {
            for (auto& pair : color_) {
                l = pair[0].process(l);
                r = pair[1].process(r);
            }
        }
        for (size_t b = 0; b < 4; ++b) {
            if (bandActive_[b]) {
                l = bands_[b][0].process(l);
                r = bands_[b][1].process(r);
            }
        }
        left[n] = l;
        right[n] = r;
    }
}

} // namespace titv::dsp
