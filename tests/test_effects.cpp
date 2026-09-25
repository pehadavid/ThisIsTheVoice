// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineHelpers.hpp"
#include "TestHarness.hpp"

#include "dsp/ColorEffects.hpp"
#include "dsp/Echo.hpp"
#include "dsp/Reverb.hpp"

using namespace titv;
using namespace titv::dsp;
using namespace titv::test;

namespace {

constexpr double kFs = 48000.0;

template <typename Module>
Stereo runStereo(Module& m, const Buffer& in, const std::function<void(size_t)>& beforeBlock = {})
{
    Stereo out { in, in };
    for (size_t pos = 0; pos < in.size(); pos += 64) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(64, in.size() - pos));
        if (beforeBlock)
            beforeBlock(pos);
        m.process(out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

template <typename Module>
void prepareAt(Module& m, float amount, double fs = kFs)
{
    m.prepare(fs);
    m.setAmount(amount);
    m.reset();
}

Stereo runEcho(Echo& e, const Buffer& in, const std::function<void(size_t)>& beforeBlock = {})
{
    Stereo out { Buffer(in.size()), Buffer(in.size()) };
    for (size_t pos = 0; pos < in.size(); pos += 64) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(64, in.size() - pos));
        if (beforeBlock)
            beforeBlock(pos);
        e.process(in.data() + pos, in.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

Stereo runReverb(Reverb& r, const Buffer& in)
{
    Stereo out { Buffer(in.size(), 0.0f), Buffer(in.size(), 0.0f) };
    for (size_t pos = 0; pos < in.size(); pos += 64) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(64, in.size() - pos));
        r.process(in.data() + pos, out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

// Level of one frequency in a buffer (Goertzel), in dB relative to a full-scale sine.
double toneDb(const Buffer& b, double freq, double fs, size_t from)
{
    const double w = 2.0 * kPi * freq / fs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < b.size(); ++i) {
        const double s = b[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    const double n = static_cast<double>(b.size() - from);
    return 10.0 * std::log10(std::max(power, 1e-30)) - 20.0 * std::log10(n / 2.0);
}

double correlation(const Buffer& a, const Buffer& b, size_t from)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < a.size(); ++i) {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(aa * bb + 1e-30);
}

size_t argmax(const Buffer& b, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < std::min(to, b.size()); ++i)
        if (std::fabs(b[i]) > std::fabs(b[best]))
            best = i;
    return best;
}

} // namespace

// --- COLOR effects ---------------------------------------------------------------

TEST(color_effects_neutral_at_zero)
{
    const Buffer in = voiceLike(kFs, 1.0, -18.0f);
    Saturator s;
    prepareAt(s, 0.0f);
    Radio r;
    prepareAt(r, 0.0f);
    Doubler d;
    prepareAt(d, 0.0f);
    Chorus c;
    prepareAt(c, 0.0f);
    CHECK(runStereo(s, in).l == in);
    CHECK(runStereo(r, in).l == in);
    CHECK(runStereo(d, in).r == in);
    CHECK(runStereo(c, in).r == in);
}

TEST(color_effects_keep_level_and_stay_finite)
{
    for (double fs : { 44100.0, 48000.0, 192000.0 }) {
        const Buffer in = voiceLike(fs, 3.0, -18.0f);
        const size_t from = static_cast<size_t>(fs);
        auto levelChange = [&](auto& m) {
            prepareAt(m, 1.0f, fs);
            const Stereo out = runStereo(m, in);
            CHECK(allFinite(out.l) && allFinite(out.r));
            return rmsDb(out.l, from) - rmsDb(in, from);
        };
        Saturator s;
        Radio r;
        Doubler d;
        Chorus c;
        CHECK(std::fabs(levelChange(s)) < 1.5f);
        CHECK(std::fabs(levelChange(r)) < 1.5f);
        CHECK(std::fabs(levelChange(d)) < 2.0f); // a widener: each side gains a little
        CHECK(std::fabs(levelChange(c)) < 1.5f);
    }
}

TEST(saturate_adds_harmonics_with_the_knob)
{
    const Buffer in = sine(500.0, kFs, 48000, 0.25f);
    double previous = -300.0;
    for (float a : { 0.0f, 0.3f, 0.6f, 1.0f }) {
        Saturator s;
        prepareAt(s, a);
        const double third = toneDb(runStereo(s, in).l, 1500.0, kFs, 4800);
        if (a == 0.0f)
            CHECK(third < -120.0);
        else
            CHECK(third > previous + 3.0);
        previous = third;
    }
    CHECK(previous > -45.0); // clearly audible at 100 %
}

TEST(radio_is_a_telephone_band)
{
    auto gainAt = [](double f) {
        Radio r;
        prepareAt(r, 1.0f);
        const Buffer in = sine(f, kFs, 48000, 0.1f);
        return rmsDb(runStereo(r, in).l, 24000) - rmsDb(in, 24000);
    };
    const float mid = gainAt(1500.0);
    CHECK(gainAt(100.0) < mid - 30.0f);
    CHECK(gainAt(10000.0) < mid - 25.0f);
}

TEST(double_widens_and_keeps_mono_sum)
{
    const Buffer in = voiceLike(kFs, 3.0, -18.0f);
    Doubler d;
    prepareAt(d, 1.0f);
    const Stereo out = runStereo(d, in);
    Buffer mono(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        mono[i] = 0.5f * (out.l[i] + out.r[i]);
    CHECK(std::fabs(rmsDb(mono, 48000) - rmsDb(in, 48000)) < 1.5f); // mono sum close to the dry voice
    const double corr = correlation(out.l, out.r, 48000);
    CHECK(corr < 0.5);  // wide in stereo
    CHECK(corr > 0.0);  // but never out of phase
}

TEST(modulation_effects_are_audible_at_low_settings)
{
    // Share of the output that is the effect, relative to the dry voice, at 25 %.
    const Buffer in = voiceLike(kFs, 3.0, -18.0f);
    auto effectDb = [&](auto& m) {
        prepareAt(m, 0.25f);
        const Stereo out = runStereo(m, in);
        Buffer diff(in.size());
        for (size_t i = 0; i < in.size(); ++i)
            diff[i] = out.l[i] - in[i];
        return rmsDb(diff, 48000) - rmsDb(in, 48000);
    };
    Chorus c;
    Doubler d;
    CHECK(effectDb(c) > -8.5f);
    CHECK(effectDb(d) > -8.0f);
}

TEST(chorus_modulation_is_smooth)
{
    for (double fs : { 48000.0, 96000.0, 192000.0 }) {
        const Buffer in = sine(440.0, fs, static_cast<size_t>(fs * 2), 0.3f);
        Chorus c;
        prepareAt(c, 1.0f, fs);
        const Stereo out = runStereo(c, in, [&](size_t pos) { c.setAmount(pos % 8192 < 4096 ? 1.0f : 0.2f); });
        CHECK(allFinite(out.l));
        // A 440 Hz sine at 0.3 moves at most ~0.017 per sample at 48 kHz; allow the wet voices.
        CHECK(maxStep(out.l, 2000) < 0.06f * 48000.0f / static_cast<float>(fs) + 0.01f);
    }
}

// --- ECHO ------------------------------------------------------------------------

namespace {

void setupEcho(Echo& e, double seconds, float feedback, float lofi = 0.0f, bool bounce = false)
{
    e.prepare(kFs);
    e.setSend(1.0f);
    e.setFeedback(feedback);
    e.setLofi(lofi);
    e.setBounce(bounce);
    e.setDelaySeconds(seconds);
    e.reset();
}

} // namespace

TEST(echo_repeats_on_time)
{
    Echo e;
    setupEcho(e, 0.25, 0.5f);
    Buffer in(48000 * 2, 0.0f);
    in[100] = 1.0f;
    const Stereo out = runEcho(e, in);
    const size_t first = argmax(out.l, 0, 18000), second = argmax(out.l, 18000, 30000);
    CHECK(first >= 100 + 12000 - 1 && first <= 100 + 12000 + 1);
    CHECK(second >= 100 + 24000 - 1 && second <= 100 + 24000 + 1);
    CHECK_NEAR(std::fabs(out.l[first]), 1.0, 0.05);
    CHECK_NEAR(std::fabs(out.l[second]) / std::fabs(out.l[first]), 0.5, 0.05);
    CHECK(out.l == out.r); // Bounce off: mono, centred
}

TEST(echo_zero_repeats_gives_one_echo)
{
    Echo e;
    setupEcho(e, 0.1, 0.0f);
    Buffer in(24000, 0.0f);
    in[0] = 1.0f;
    const Stereo out = runEcho(e, in);
    CHECK(peak(Buffer(out.l.begin() + 4000, out.l.begin() + 6000)) > 0.9f);
    CHECK(peak(Buffer(out.l.begin() + 6000, out.l.end())) < 1e-3f);
}

TEST(echo_bounce_alternates_sides)
{
    Echo e;
    setupEcho(e, 0.1, 0.7f, 0.0f, true);
    Buffer in(24000, 0.0f);
    in[0] = 1.0f;
    const Stereo out = runEcho(e, in);
    auto window = [](const Buffer& b, size_t at) { return peak(Buffer(b.begin() + at - 50, b.begin() + at + 50)); };
    CHECK(window(out.l, 4800) > 0.9f && window(out.r, 4800) < 1e-3f);        // 1st repeat: left
    CHECK(window(out.r, 9600) > 0.6f && window(out.l, 9600) < 1e-3f);        // 2nd: right
    CHECK(window(out.l, 14400) > 0.4f && window(out.r, 14400) < 1e-3f);      // 3rd: left
}

TEST(echo_lofi_filters_the_repeats_only)
{
    auto echoLevel = [](float lofi, double freq) {
        Echo e;
        setupEcho(e, 0.1, 0.0f, lofi);
        Buffer in = sine(freq, kFs, 24000, 0.2f);
        std::fill(in.begin() + 2400, in.end(), 0.0f); // 50 ms burst
        const Stereo out = runEcho(e, in);
        return rmsDb(out.l, 4800 + 600, 4800 + 2400);
    };
    CHECK(std::fabs(echoLevel(0.0f, 100.0) - echoLevel(0.0f, 1200.0)) < 0.5f); // full band at 0 %
    CHECK(echoLevel(1.0f, 100.0) < echoLevel(0.0f, 100.0) - 20.0f);
    CHECK(echoLevel(1.0f, 10000.0) < echoLevel(0.0f, 10000.0) - 20.0f);
}

TEST(echo_time_change_crossfades)
{
    Echo e;
    setupEcho(e, 0.25, 0.6f);
    const Buffer in = sine(220.0, kFs, 48000 * 4, 0.2f);
    const Stereo out = runEcho(e, in, [&](size_t pos) {
        if (pos % 9600 == 0) // a new time every 200 ms, as under tempo automation
            e.setDelaySeconds(pos % 19200 == 0 ? 0.13 : 0.31);
    });
    CHECK(allFinite(out.l));
    // A jump of the read position would step by up to ~0.5; a crossfade stays near the sine slope.
    CHECK(maxStep(out.l, 24000) < 0.05f);
}

TEST(echo_high_feedback_stays_bounded)
{
    Echo e;
    setupEcho(e, 0.05, 0.95f);
    const Buffer in = sine(300.0, kFs, 48000 * 5, 0.9f);
    const Stereo out = runEcho(e, in, [&](size_t pos) { e.setFeedback(pos % 4800 < 2400 ? 0.95f : 0.2f); });
    CHECK(allFinite(out.l) && allFinite(out.r));
    // The loop soft clip holds it under full scale; the interpolated read may overshoot a
    // little, but nothing runs away.
    CHECK(peak(out.l) < 1.2f);
}

TEST(echo_tail_rings_after_send_off_then_sleeps)
{
    Echo e;
    setupEcho(e, 0.2, 0.6f);
    Buffer in(48000 * 20, 0.0f);
    in[0] = 1.0f;
    const Stereo out = runEcho(e, in, [&](size_t pos) {
        if (pos == 64)
            e.setSend(0.0f); // send closed right after the impulse
    });
    CHECK(peak(Buffer(out.l.begin() + 9000, out.l.begin() + 10000)) > 0.9f);  // 1st repeat
    CHECK(peak(Buffer(out.l.begin() + 28000, out.l.begin() + 30000)) > 0.1f); // tail still ringing
    CHECK(e.isSleeping());                                                      // then asleep
}

// --- SPACE -----------------------------------------------------------------------

TEST(reverbs_decay_as_designed_and_sleep)
{
    for (const Reverb::Design* d : { &Reverb::kRoom, &Reverb::kPlate, &Reverb::kHall, &Reverb::kAmbient }) {
        for (double fs : { 44100.0, 96000.0 }) {
            Reverb r(*d);
            r.prepare(fs);
            const size_t burst = static_cast<size_t>(0.5 * fs);
            Buffer in = whiteNoise(burst, 9, 0.5f);
            in.resize(burst + static_cast<size_t>((d->rt60Seconds * 2.5 + 1.0) * fs), 0.0f);
            const Stereo out = runReverb(r, in);
            CHECK(allFinite(out.l) && allFinite(out.r));

            // Decay rate over 0.1 .. 0.1 + RT60/3 after the burst, extrapolated to 60 dB.
            const size_t a = burst + static_cast<size_t>(0.1 * fs);
            const size_t b = a + static_cast<size_t>(d->rt60Seconds / 3.0 * fs);
            const size_t w = static_cast<size_t>(0.05 * fs);
            const double drop = rmsDb(out.l, a, a + w) - rmsDb(out.l, b, b + w);
            const double rt60 = 60.0 * (d->rt60Seconds / 3.0) / drop;
            if (d != &Reverb::kRoom && !(rt60 > 0.7 * d->rt60Seconds && rt60 < 1.3 * d->rt60Seconds))
                std::printf("    %s at %.0f Hz: RT60 %.2f s (design %.2f s)\n", d->name, fs, rt60, d->rt60Seconds);
            if (d != &Reverb::kRoom) // Room is mostly early reflections
                CHECK(rt60 > 0.7 * d->rt60Seconds && rt60 < 1.3 * d->rt60Seconds);
            CHECK(correlation(out.l, out.r, burst) < 0.5); // wide return
            CHECK(r.isSleeping());
        }
    }
}
