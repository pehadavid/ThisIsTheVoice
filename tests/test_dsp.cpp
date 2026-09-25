// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineHelpers.hpp"
#include "TestHarness.hpp"

#include "dsp/AutoLevel.hpp"
#include "dsp/DeEsser.hpp"
#include "dsp/ToneEq.hpp"
#include "dsp/VoiceCompressor.hpp"

#include <limits>

using namespace titv;
using namespace titv::dsp;
using namespace titv::test;

namespace {

constexpr double kFs = 48000.0;

// Runs a stereo module on a mono signal fed to both channels, in 64-sample blocks.
// Calls onBlock after each block, e.g. to read a meter.
template <typename Module>
Buffer run(Module& m, const Buffer& in, const std::function<void(size_t)>& beforeBlock = {},
           const std::function<void()>& onBlock = {})
{
    Buffer l = in, r = in;
    for (size_t pos = 0; pos < in.size(); pos += 64) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(64, in.size() - pos));
        if (beforeBlock)
            beforeBlock(pos);
        m.process(l.data() + pos, r.data() + pos, n);
        if (onBlock)
            onBlock();
    }
    return l;
}

double db(double linear) { return 20.0 * std::log10(linear); }

} // namespace

// --- Filters ---------------------------------------------------------------------

TEST(biquad_shapes)
{
    CHECK_NEAR(db(BiquadCoeffs::peaking(1000, 0.9, 6, kFs).magnitudeAt(1000, kFs)), 6.0, 1e-6);
    CHECK_NEAR(db(BiquadCoeffs::peaking(1000, 0.9, -9, kFs).magnitudeAt(1000, kFs)), -9.0, 1e-6);
    CHECK_NEAR(db(BiquadCoeffs::lowShelf(180, 0.707, 6, kFs).magnitudeAt(10, kFs)), 6.0, 0.02);
    CHECK_NEAR(db(BiquadCoeffs::lowShelf(180, 0.707, 6, kFs).magnitudeAt(8000, kFs)), 0.0, 0.02);
    CHECK_NEAR(db(BiquadCoeffs::lowShelf(180, 0.707, 6, kFs).magnitudeAt(180, kFs)), 3.0, 0.05);
    CHECK_NEAR(db(BiquadCoeffs::highShelf(12000, 0.707, -8, kFs).magnitudeAt(23900, kFs)), -8.0, 0.2);
    CHECK_NEAR(db(BiquadCoeffs::highShelf(12000, 0.707, -8, kFs).magnitudeAt(100, kFs)), 0.0, 0.02);
    CHECK_NEAR(db(BiquadCoeffs::bandPass(7000, 0.9, kFs).magnitudeAt(7000, kFs)), 0.0, 1e-6);
    CHECK(BiquadCoeffs::bandPass(7000, 0.9, kFs).magnitudeAt(500, kFs) < 0.1);
    CHECK_NEAR(db(BiquadCoeffs::lowPass(10000, 0.707, kFs).magnitudeAt(10000, kFs)), -3.01, 0.01);
}

// --- TONE ------------------------------------------------------------------------

TEST(tone_bands_match_design)
{
    struct Case {
        int band;
        double freq;
        BiquadCoeffs expected;
    };
    const Case cases[] = {
        { 0, 100.0, BiquadCoeffs::lowShelf(ToneEq::kBody.freq, ToneEq::kBody.q, 6, kFs) },
        { 1, 1200.0, BiquadCoeffs::peaking(ToneEq::kMid.freq, ToneEq::kMid.q, 6, kFs) },
        { 2, 4500.0, BiquadCoeffs::peaking(ToneEq::kPresence.freq, ToneEq::kPresence.q, 6, kFs) },
        { 1, 600.0, BiquadCoeffs::peaking(ToneEq::kMid.freq, ToneEq::kMid.q, 6, kFs) },
    };
    for (const Case& c : cases) {
        ToneEq eq;
        eq.prepare(kFs);
        eq.setTonalColor(false);
        float g[4] = { 0, 0, 0, 0 };
        g[c.band] = 6.0f;
        eq.setGains(g[0], g[1], g[2], g[3]);
        eq.reset();
        const Buffer in = sine(c.freq, kFs, 48000, 0.1f);
        const Buffer out = run(eq, in);
        CHECK_NEAR(rmsDb(out, 24000) - rmsDb(in, 24000), db(c.expected.magnitudeAt(c.freq, kFs)), 0.05);
    }
}

TEST(tonal_color_curve_response)
{
    for (double f : { 100.0, 280.0, 1000.0, 2800.0, 9000.0 }) {
        ToneEq eq;
        eq.prepare(kFs);
        eq.setGains(0, 0, 0, 0);
        eq.reset();
        const Buffer in = sine(f, kFs, 48000, 0.1f);
        const Buffer out = run(eq, in);
        double expected = 0.0;
        for (const auto& b : ToneEq::kColorCurve)
            expected += db(ToneEq::colorCoeffs(b, kFs).magnitudeAt(f, kFs));
        CHECK_NEAR(rmsDb(out, 24000) - rmsDb(in, 24000), expected, 0.05);
        // Subtle by design: never more than 2 dB either way.
        CHECK(std::fabs(expected) <= 2.0);
    }
}

TEST(air_backs_off_on_sibilance)
{
    auto appliedAir = [](const Buffer& in) {
        ToneEq eq;
        eq.prepare(kFs);
        eq.setGains(0, 0, 0, 9.0f);
        eq.reset();
        float minApplied = 100.0f;
        size_t blocks = 0;
        run(eq, in, {}, [&] {
            if (++blocks > 200) // after the first 0.27 s
                minApplied = std::min(minApplied, eq.airAppliedDb());
        });
        return minApplied;
    };
    Buffer v = vowel(kFs, 48000);
    scaleToRmsDb(v, -20.0f);
    Buffer s = sibilantNoise(kFs, 48000);
    scaleToRmsDb(s, -20.0f);

    CHECK_NEAR(appliedAir(v), 9.0, 0.01);        // voiced sound: full boost
    CHECK(appliedAir(s) < 9.0f * (1.0f - 0.6f)); // sibilance: most of the boost removed
    CHECK(appliedAir(s) >= 9.0f * (1.0f - ToneEq::kAirMaxReduction) - 0.01f);
}

TEST(tone_survives_fast_automation)
{
    for (double fs : { 44100.0, 192000.0 }) {
        ToneEq eq;
        eq.prepare(fs);
        eq.reset();
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> gain(-15.0f, 15.0f);
        const Buffer in = whiteNoise(static_cast<size_t>(fs), 11, 0.3f);
        const Buffer out = run(eq, in, [&](size_t) { eq.setGains(gain(rng), gain(rng), gain(rng), gain(rng)); });
        CHECK(allFinite(out));
        CHECK(peak(out) < 10.0f);
    }
}

// --- De-esser --------------------------------------------------------------------

TEST(deesser_is_neutral_at_zero)
{
    DeEsser d;
    d.prepare(kFs);
    d.setAmount(0.0f);
    d.reset();
    const Buffer in = voiceLike(kFs, 2.0, -18.0f);
    CHECK(run(d, in) == in);
    CHECK(d.reductionDb() == 0.0f);
}

TEST(deesser_targets_sibilance_only)
{
    auto maxReduction = [](const Buffer& in, float amount) {
        DeEsser d;
        d.prepare(kFs);
        d.setAmount(amount);
        d.reset();
        float m = 0.0f;
        const Buffer out = run(d, in, {}, [&] { m = std::max(m, d.reductionDb()); });
        CHECK(allFinite(out));
        return m;
    };
    Buffer s = sibilantNoise(kFs, 48000);
    Buffer v = vowel(kFs, 48000);
    for (float level : { -30.0f, -12.0f }) { // independent of the input level
        scaleToRmsDb(s, level);
        scaleToRmsDb(v, level);
        CHECK(maxReduction(s, 0.8f) > 6.0f);
        CHECK(maxReduction(v, 0.8f) < 0.5f);
        CHECK(maxReduction(s, 0.2f) < maxReduction(s, 0.8f));
        CHECK(maxReduction(s, 1.0f) <= DeEsser::maxReductionDb(1.0f) + 0.01f);
    }
}

// --- COMPRESS --------------------------------------------------------------------

TEST(compressor_curve)
{
    CHECK(compressorGainDb(-40, -20, 4, 8) == 0.0f);
    CHECK_NEAR(compressorGainDb(0, -20, 4, 8), -15.0, 1e-5);   // 20 dB over at 4:1
    CHECK_NEAR(compressorGainDb(-24, -20, 4, 8), 0.0, 1e-6);   // knee start
    CHECK_NEAR(compressorGainDb(-16, -20, 4, 8), -3.0, 1e-5);  // knee end joins the line
    CHECK(compressorGainDb(-20, -20, 4, 8) < 0.0f && compressorGainDb(-20, -20, 4, 8) > -1.0f);
    CHECK(compressorGainDb(10, -20, 1, 8) == 0.0f);             // ratio 1: no change
    CHECK_NEAR(softClip(0.5f, 0.85f), 0.5, 0.0);
    CHECK(softClip(100.0f, 0.85f) <= 1.0f && softClip(-100.0f, 0.85f) >= -1.0f);
}

TEST(compressor_is_neutral_at_zero)
{
    VoiceCompressor c;
    c.prepare(kFs);
    c.setAmount(0.0f);
    c.reset();
    const Buffer in = voiceLike(kFs, 2.0, -18.0f);
    CHECK(run(c, in) == in);
}

TEST(compressor_keeps_loudness_comparable)
{
    // Comparable output level whatever the macro, for a voice at the
    // working level (-18 dBFS RMS).
    const Buffer in = voiceLike(kFs, 6.0, -18.0f);
    for (float amount : { 0.0f, 0.25f, 0.5f, 0.7f, 1.0f }) {
        VoiceCompressor c;
        c.prepare(kFs);
        c.setAmount(amount);
        c.reset();
        const Buffer out = run(c, in);
        const float diff = rmsDb(out, 48000) - rmsDb(in, 48000);
        std::printf("    COMPRESS %3.0f %%: %+.2f dB\n", amount * 100, diff);
        CHECK(std::fabs(diff) < 1.5f);
    }
}

TEST(compressor_reduces_dynamics)
{
    // Alternating 1 s phrases 18 dB apart.
    Buffer in = voiceLike(kFs, 8.0, -18.0f);
    for (size_t i = 0; i < in.size(); ++i)
        if ((i / 48000) % 2 == 1)
            in[i] *= dsp::dbToGain(-18.0f);

    auto spread = [&](float amount) {
        VoiceCompressor c;
        c.prepare(kFs);
        c.setAmount(amount);
        c.reset();
        const Buffer out = run(c, in);
        // Loud phrase 3 vs quiet phrase 4, skipping the attack of each.
        return rmsDb(out, 2 * 48000 + 9600, 3 * 48000) - rmsDb(out, 3 * 48000 + 9600, 4 * 48000);
    };
    const float dry = spread(0.0f), mid = spread(0.5f), full = spread(1.0f);
    std::printf("    loud/quiet spread: 0 %% %.1f dB, 50 %% %.1f dB, 100 %% %.1f dB\n", dry, mid, full);
    CHECK_NEAR(dry, 18.0, 0.5);
    CHECK(mid < dry - 4.0f);
    CHECK(full < mid - 2.0f);
}

TEST(compressor_contains_peaks)
{
    for (float amount : { 0.1f, 0.7f, 1.0f }) {
        VoiceCompressor c;
        c.prepare(kFs);
        c.setAmount(amount);
        c.reset();
        Buffer in = sine(200.0, kFs, 48000, 4.0f); // +12 dBFS
        in[20000] = 20.0f;                          // and a spike
        const Buffer out = run(c, in);
        // The clipper tends to full scale; in float it can reach it, never exceed it.
        CHECK(peak(out) <= 1.0f);
        CHECK(c.reductionDb() > 3.0f);
    }
}

TEST(compressor_macro_automation_is_smooth)
{
    const Buffer in = voiceLike(kFs, 3.0, -18.0f);
    VoiceCompressor fixed;
    fixed.prepare(kFs);
    fixed.setAmount(1.0f);
    fixed.reset();
    const float reference = maxStep(run(fixed, in), 4800);

    VoiceCompressor c;
    c.prepare(kFs);
    c.reset();
    bool high = false;
    const Buffer out = run(c, in, [&](size_t pos) {
        if (pos % 1024 == 0) {
            high = !high;
            c.setAmount(high ? 1.0f : 0.0f);
        }
    });
    CHECK(allFinite(out));
    CHECK(maxStep(out, 4800) < 1.5f * reference);
}

// --- Auto Level ------------------------------------------------------------------

namespace {

void feed(AutoLevel& a, const Buffer& in, size_t block = 64)
{
    for (size_t pos = 0; pos < in.size(); pos += block) {
        const uint32_t n = static_cast<uint32_t>(std::min(block, in.size() - pos));
        a.process(in.data() + pos, in.data() + pos, n, false);
    }
}

} // namespace

TEST(auto_level_measures_voice)
{
    AutoLevel a;
    a.prepare(kFs);
    CHECK(a.state() == AutoLevel::State::Idle);

    Buffer voice = sine(220.0, kFs, static_cast<size_t>(11 * kFs));
    scaleToRmsDb(voice, -30.0f);

    a.start();
    feed(a, Buffer(static_cast<size_t>(3 * kFs), 0.0f)); // silence does not count
    CHECK(a.state() == AutoLevel::State::Listening);
    CHECK(a.progress() == 0.0f);

    feed(a, Buffer(voice.begin(), voice.begin() + static_cast<long>(5 * kFs)));
    CHECK(a.state() == AutoLevel::State::Listening);
    CHECK_NEAR(a.progress(), 0.5, 0.02);

    feed(a, voice);
    CHECK(a.state() == AutoLevel::State::Done);
    const float measured = a.takeResult();
    CHECK_NEAR(measured, -30.0, 0.1);
    CHECK(a.state() == AutoLevel::State::Idle);
    CHECK_NEAR(AutoLevel::gainFor(measured, -24, 24), 12.0, 0.1);
    CHECK(AutoLevel::gainFor(-70.0f, -24, 24) == 24.0f);
    CHECK(AutoLevel::gainFor(10.0f, -24, 24) == -24.0f);
}

TEST(auto_level_ignores_clicks_and_bad_samples)
{
    AutoLevel a;
    a.prepare(kFs);
    Buffer voice = sine(220.0, kFs, static_cast<size_t>(30 * kFs));
    scaleToRmsDb(voice, -30.0f);
    // A full-scale click every 100 ms: every other window is rejected.
    for (size_t i = 1200; i < voice.size(); i += 4800)
        voice[i] = 1.0f;

    a.start();
    feed(a, Buffer(voice.begin(), voice.begin() + static_cast<long>(10 * kFs)));
    CHECK(a.state() == AutoLevel::State::Listening);
    CHECK_NEAR(a.progress(), 0.5, 0.02);

    // Blocks flagged with non-finite samples do not count either.
    const size_t before = static_cast<size_t>(a.progress() * AutoLevel::kWindowsNeeded);
    for (size_t pos = 0; pos < static_cast<size_t>(2 * kFs); pos += 64)
        a.process(voice.data() + pos, voice.data() + pos, 64, true);
    CHECK(static_cast<size_t>(a.progress() * AutoLevel::kWindowsNeeded) == before);

    feed(a, Buffer(voice.begin() + static_cast<long>(10 * kFs), voice.end()));
    CHECK(a.state() == AutoLevel::State::Done);
    CHECK_NEAR(a.takeResult(), -30.0, 0.1); // the clicks did not raise the estimate
}

TEST(auto_level_cancel)
{
    AutoLevel a;
    a.prepare(kFs);
    const Buffer voice = sine(220.0, kFs, static_cast<size_t>(2 * kFs), 0.1f);
    a.start();
    feed(a, voice);
    CHECK(a.state() == AutoLevel::State::Listening && a.progress() > 0.1f);
    a.cancel();
    feed(a, Buffer(64, 0.0f));
    CHECK(a.state() == AutoLevel::State::Idle);
    // Restarting begins from scratch.
    a.start();
    feed(a, Buffer(64, 0.0f));
    CHECK(a.state() == AutoLevel::State::Listening && a.progress() == 0.0f);
}
