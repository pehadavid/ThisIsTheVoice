// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineHelpers.hpp"
#include "TestHarness.hpp"

#include <limits>
#include <random>

using namespace titv;
using namespace titv::test;

namespace {

constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

// Engine holds atomics and cannot be moved, so tests prepare it in place.
void prepare(Engine& e, double sampleRate = 48000.0, uint32_t maxBlock = 512)
{
    e.prepare(sampleRate, maxBlock);
}

// Dry path only: COMPRESS at 0 % and the TONE / COLOR sections off, which the
// engine guarantees to be bit-exact bypasses.
void neutral(Engine& e)
{
    e.setParameter(Param::CompressAmount, 0.0f);
    e.setParameter(Param::ToneEnabled, 0.0f);
    e.setParameter(Param::ColorEnabled, 0.0f);
    e.reset();
}

// Dry-path HPF response: the 90 Hz input filter and the 100 Hz output filter in cascade.
double hpfCascadeDb(double freq, double sampleRate)
{
    const auto a = dsp::BiquadCoeffs::highPass(Engine::kInputHpfHz, Engine::kHpfQ, sampleRate);
    const auto b = dsp::BiquadCoeffs::highPass(Engine::kOutputHpfHz, Engine::kHpfQ, sampleRate);
    return 20.0 * std::log10(a.magnitudeAt(freq, sampleRate) * b.magnitudeAt(freq, sampleRate));
}

} // namespace

TEST(silence_in_silence_out)
{
    for (double fs : kRates) {
        Engine e;
        prepare(e, fs);
        const Buffer in(4096, 0.0f);
        const Stereo out = render(e, in, &in, fixedBlocks(64));
        CHECK(std::all_of(out.l.begin(), out.l.end(), [](float x) { return x == 0.0f; }));
        CHECK(std::all_of(out.r.begin(), out.r.end(), [](float x) { return x == 0.0f; }));
    }
}

TEST(identity_when_hpf_off_zero_latency)
{
    Engine e;
    prepare(e);
    e.setParameter(Param::HpfEnabled, 0.0f);
    neutral(e);
    Buffer in = sine(440.0, 48000.0, 4096);
    in[0] = 1.0f;
    const Stereo out = render(e, in, &in, fixedBlocks(64));
    CHECK(out.l == in);
    CHECK(out.r == in);
}

TEST(impulse_peak_at_sample_zero_with_hpf)
{
    for (double fs : kRates) {
        Engine e;
        prepare(e, fs);
        Buffer in(8192, 0.0f);
        in[0] = 1.0f;
        const Stereo out = render(e, in, &in, fixedBlocks(64));
        size_t peak = 0;
        for (size_t i = 1; i < out.l.size(); ++i)
            if (std::fabs(out.l[i]) > std::fabs(out.l[peak]))
                peak = i;
        CHECK(peak == 0);
        CHECK(Engine::latencySamples() == 0);
    }
}

TEST(hpf_cascade_matches_design)
{
    // Design target: about -7 dB at 90 Hz, -5 dB at 100 Hz and -3 dB near 120 Hz.
    CHECK_NEAR(hpfCascadeDb(90.0, 48000.0), -7.0, 0.1);
    CHECK_NEAR(hpfCascadeDb(100.0, 48000.0), -5.2, 0.1);
    CHECK_NEAR(hpfCascadeDb(120.0, 48000.0), -2.9, 0.15);

    for (double fs : kRates) {
        for (double f : { 30.0, 90.0, 120.0, 1000.0, 8000.0 }) {
            Engine e;
            prepare(e, fs);
            neutral(e);
            const size_t frames = static_cast<size_t>(fs * 1.5);
            const Buffer in = sine(f, fs, frames);
            const Stereo out = render(e, in, &in, fixedBlocks(256));
            const size_t settle = static_cast<size_t>(fs * 0.5);
            const double measured = 20.0 * std::log10(rms(out.l, settle) / rms(in, settle));
            CHECK_NEAR(measured, hpfCascadeDb(f, fs), 0.05);
        }
    }
}

TEST(output_independent_of_block_size)
{
    std::mt19937 rng(1234);
    std::uniform_int_distribution<uint32_t> dist(1, 700);
    const Buffer in = sine(220.0, 48000.0, 48000, 0.8f);

    auto run = [&](const std::function<uint32_t()>& blocks) {
        Engine e;
        prepare(e, 48000.0, 512);
        e.setParameter(Param::InputGainDb, 6.0f);
        e.setParameter(Param::OutputGainDb, -3.0f);
        e.reset();
        return render(e, in, &in, blocks);
    };

    const Stereo ref = run(fixedBlocks(64));
    for (uint32_t n : { 1u, 7u, 512u, 4096u }) {
        const Stereo out = run(fixedBlocks(n));
        CHECK(out.l == ref.l && out.r == ref.r);
    }
    const Stereo random = run([&] { return dist(rng); });
    CHECK(random.l == ref.l && random.r == ref.r);
}

TEST(non_finite_input_is_neutralised)
{
    Engine e;
    prepare(e);
    Buffer in = sine(1000.0, 48000.0, 9600);
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[101] = std::numeric_limits<float>::infinity();
    in[102] = -std::numeric_limits<float>::infinity();
    const Stereo out = render(e, in, &in, fixedBlocks(64));
    CHECK(allFinite(out.l) && allFinite(out.r));
    // The chain keeps working afterwards.
    CHECK(rms(out.l, 4800) > 0.1f);
}

TEST(gain_automation_is_smooth)
{
    // Worst case: jumps between -24 and +24 dB every 256 samples on a low sine.
    const double fs = 48000.0;
    const Buffer in = sine(100.0, fs, 48000, 0.25f);
    for (Param p : { Param::InputGainDb, Param::OutputGainDb }) {
        Engine e;
        prepare(e, fs);
        e.setParameter(Param::HpfEnabled, 0.0f);
        e.reset();
        bool high = false;
        const Stereo out = render(e, in, &in, fixedBlocks(64), [&](size_t pos) {
            if (pos % 256 == 0) {
                high = !high;
                e.setParameter(p, high ? info(p).max : -24.0f);
            }
        });
        // A step of the gain would jump by up to about 4; the smoothed ramp stays far below.
        CHECK(maxStep(out.l) < 0.06f);
        CHECK(allFinite(out.l));
    }
}

TEST(bypass_crossfades_to_exact_input)
{
    const double fs = 48000.0;
    Engine e;
    prepare(e, fs);
    e.setParameter(Param::InputGainDb, 12.0f);
    e.reset();
    const Buffer in = sine(100.0, fs, 24000, 0.25f);
    const Stereo out = render(e, in, &in, fixedBlocks(64), [&](size_t pos) {
        if (pos == 4800)
            e.setParameter(Param::GlobalBypass, 1.0f);
        if (pos == 14400)
            e.setParameter(Param::GlobalBypass, 0.0f);
    });
    // Around both switches, the output moves no faster than the sine itself.
    CHECK(maxStep(out.l, 4700, 5400) < 0.02f);
    CHECK(maxStep(out.l, 14300, 15000) < 0.02f);
    // Fully bypassed once the 10 ms fade is over: bit-exact dry signal.
    for (size_t i = 4800 + 480; i < 14400; ++i)
        CHECK(out.l[i] == in[i] && out.r[i] == in[i]);
}

TEST(hpf_toggle_is_smooth)
{
    const double fs = 48000.0;
    Engine e;
    prepare(e, fs);
    const Buffer in = sine(40.0, fs, 48000, 0.5f);
    const Stereo out = render(e, in, &in, fixedBlocks(128), [&](size_t pos) {
        if (pos % 4096 == 0)
            e.setParameter(Param::HpfEnabled, e.parameter(Param::HpfEnabled) < 0.5f ? 1.0f : 0.0f);
    });
    CHECK(maxStep(out.l) < 0.02f);
}

TEST(mono_input_feeds_both_outputs)
{
    Engine e;
    prepare(e);
    const Buffer in = sine(300.0, 48000.0, 4800);
    const Stereo mono = render(e, in, nullptr, fixedBlocks(64));
    CHECK(mono.l == mono.r);

    Engine s;
    prepare(s);
    const Stereo stereo = render(s, in, &in, fixedBlocks(64));
    CHECK(mono.l == stereo.l);
}

TEST(mono_and_multichannel_outputs)
{
    Engine e;
    prepare(e);
    e.setParameter(Param::HpfEnabled, 0.0f);
    neutral(e);
    const Buffer l = sine(300.0, 48000.0, 256, 0.5f);
    const Buffer r(256, 0.25f);
    const float* ins[2] = { l.data(), r.data() };

    Buffer mono(256);
    float* monoOut[1] = { mono.data() };
    e.process(ins, 2, monoOut, 1, 256);
    for (size_t i = 0; i < 256; ++i)
        CHECK_NEAR(mono[i], 0.5f * (l[i] + r[i]), 1e-7);

    Buffer a(256), b(256), c(256, 9.0f), d(256, 9.0f);
    float* quad[4] = { a.data(), b.data(), c.data(), d.data() };
    e.process(ins, 2, quad, 4, 256);
    CHECK(std::all_of(c.begin(), c.end(), [](float x) { return x == 0.0f; }));
    CHECK(std::all_of(d.begin(), d.end(), [](float x) { return x == 0.0f; }));
}

TEST(in_place_processing)
{
    const Buffer in = sine(500.0, 48000.0, 2048);
    Engine a;
    prepare(a);
    const Stereo ref = render(a, in, &in, fixedBlocks(64));

    Engine b;
    prepare(b);
    Buffer l = in, r = in;
    for (size_t pos = 0; pos < l.size(); pos += 64) {
        float* io[2] = { l.data() + pos, r.data() + pos };
        b.process(io, 2, io, 2, 64);
    }
    CHECK(l == ref.l && r == ref.r);
}

TEST(parameter_only_calls)
{
    Engine e;
    prepare(e);
    e.process(nullptr, 0, nullptr, 0, 0);
    e.setParameter(Param::OutputGainDb, -6.0f);
    e.process(nullptr, 0, nullptr, 0, 128);
    CHECK(e.parameter(Param::OutputGainDb) == -6.0f);

    // No input buffers: silence in, outputs still written.
    Buffer l(64, 1.0f), r(64, 1.0f);
    float* outs[2] = { l.data(), r.data() };
    e.process(nullptr, 0, outs, 2, 64);
    CHECK(std::all_of(l.begin(), l.end(), [](float x) { return x == 0.0f; }));
}

TEST(tempo_fallback)
{
    Engine e;
    CHECK(e.tempo() == kFallbackTempoBpm && e.tempoIsFallback());
    e.setTempo(92.5, true);
    CHECK(e.tempo() == 92.5 && !e.tempoIsFallback());
    // No extrapolation of the last tempo once the host stops sending one.
    e.setTempo(92.5, false);
    CHECK(e.tempo() == kFallbackTempoBpm && e.tempoIsFallback());
    e.setTempo(std::numeric_limits<double>::quiet_NaN(), true);
    CHECK(e.tempoIsFallback());
}

TEST(meters)
{
    const double fs = 48000.0;
    Engine e;
    prepare(e, fs);
    e.setParameter(Param::HpfEnabled, 0.0f);
    e.setParameter(Param::InputGainDb, 6.0f);
    neutral(e);
    const Buffer in = sine(1000.0, fs, 96000, 0.25f);
    render(e, in, &in, fixedBlocks(64));

    const float expectedRms = 0.25f * dsp::dbToGain(6.0f) / std::sqrt(2.0f);
    CHECK_NEAR(e.inputMeter().rms(), expectedRms, 0.01);
    CHECK_NEAR(e.outputMeter().rms(), expectedRms, 0.01);
    CHECK_NEAR(e.inputMeter().peak(), 0.25f * dsp::dbToGain(6.0f), 0.01);
    CHECK(!e.inputMeter().clipped());

    const Buffer loud = sine(1000.0, fs, 4800, 0.9f);
    render(e, loud, &loud, fixedBlocks(64));
    CHECK(e.inputMeter().clipped() && e.outputMeter().clipped());
    e.inputMeter().clearClip();
    CHECK(!e.inputMeter().clipped());
}

TEST(reset_clears_filter_state)
{
    Engine e;
    prepare(e);
    const Buffer loud = sine(60.0, 48000.0, 4800, 0.9f);
    render(e, loud, &loud, fixedBlocks(64));
    e.reset();
    const Buffer silence(1024, 0.0f);
    const Stereo out = render(e, silence, &silence, fixedBlocks(64));
    CHECK(std::all_of(out.l.begin(), out.l.end(), [](float x) { return x == 0.0f; }));
}

TEST(full_chain_is_finite_at_every_rate)
{
    for (double fs : kRates) {
        Engine e;
        prepare(e, fs);
        e.setParameter(Param::ToneBodyDb, 6.0f);
        e.setParameter(Param::TonePresenceDb, -4.0f);
        e.setParameter(Param::ToneAirDb, 9.0f);
        e.setParameter(Param::ColorDeess, 60.0f);
        e.setParameter(Param::CompressAmount, 100.0f);
        e.reset();
        Buffer in = voiceLike(fs, 2.0, -12.0f);
        in[1000] = std::numeric_limits<float>::quiet_NaN();
        const Stereo out = render(e, in, &in, fixedBlocks(128));
        CHECK(allFinite(out.l) && allFinite(out.r));
        // The ceiling is set inside COMPRESS; the de-esser and the output HPF shift the
        // phase afterwards, so peaks may exceed it slightly (< 1 dB), not more.
        CHECK(peak(out.l) < dsp::dbToGain(1.0f));
        CHECK(e.compressorReductionDb() >= 0.0f);
    }
}

TEST(section_switches_are_smooth)
{
    const double fs = 48000.0;
    Engine e;
    prepare(e, fs);
    e.setParameter(Param::ToneBodyDb, 12.0f);
    e.setParameter(Param::ToneAirDb, -12.0f);
    e.setParameter(Param::ColorDeess, 100.0f);
    e.reset();
    const Buffer in = sine(150.0, fs, 48000, 0.2f);
    const Stereo out = render(e, in, &in, fixedBlocks(64), [&](size_t pos) {
        if (pos % 4096 == 0) {
            e.setParameter(Param::ToneEnabled, e.parameter(Param::ToneEnabled) < 0.5f ? 1.0f : 0.0f);
            e.setParameter(Param::ColorEnabled, e.parameter(Param::ColorEnabled) < 0.5f ? 1.0f : 0.0f);
        }
    });
    CHECK(maxStep(out.l, 4800) < 0.06f);
}

TEST(compress_zero_and_sections_off_is_bit_exact)
{
    Engine e;
    prepare(e);
    e.setParameter(Param::HpfEnabled, 0.0f);
    e.setParameter(Param::ToneAirDb, 10.0f);
    e.setParameter(Param::ColorDeess, 80.0f);
    neutral(e);
    const Buffer in = voiceLike(48000.0, 1.0, -18.0f);
    CHECK(render(e, in, &in, fixedBlocks(64)).l == in);
}

TEST(slow_input_ramp_for_auto_level)
{
    const double fs = 48000.0;
    auto levelAfter = [&](bool slow, double seconds) {
        Engine e;
        prepare(e, fs);
        e.setParameter(Param::HpfEnabled, 0.0f);
        neutral(e);
        const Buffer in(static_cast<size_t>(seconds * fs), 0.1f); // DC, HPF off
        if (slow)
            e.requestSlowInputRamp();
        e.setParameter(Param::InputGainDb, 12.0f);
        return render(e, in, &in, fixedBlocks(64)).l.back() / 0.1f;
    };
    const float target = dsp::dbToGain(12.0f);
    CHECK(levelAfter(false, 0.1) > 0.99f * target);                           // normal: ~20 ms
    CHECK(levelAfter(true, 0.1) < 0.8f * target);                             // slow: still moving
    CHECK(levelAfter(true, 0.35) > 0.95f * target);                           // applied over 100-300 ms
}

TEST(extreme_sample_rates_stay_finite)
{
    // clap-validator tries rates from 1 kHz to 768 kHz, fractional ones included.
    for (double fs : { 1000.0, 8000.0, 12345.678, 22050.0, 384000.0, 768000.0 }) {
        Engine e;
        prepare(e, fs, 256);
        e.setParameter(Param::ToneAirDb, 12.0f);
        e.setParameter(Param::ToneBodyDb, 12.0f);
        e.setParameter(Param::ColorDeess, 100.0f);
        e.reset();
        const Buffer in = whiteNoise(static_cast<size_t>(fs * 0.5) + 256, 5, 0.8f);
        const Stereo out = render(e, in, &in, fixedBlocks(256));
        CHECK(allFinite(out.l) && allFinite(out.r));
        CHECK(peak(out.l) < 4.0f);
    }
}

namespace {

// Neutral dry path (HPF off too), so that the returns can be compared with the
// effect modules on their own.
void dryOnly(Engine& e)
{
    e.setParameter(Param::HpfEnabled, 0.0f);
    neutral(e);
}

} // namespace

TEST(space_send_off_lets_the_tail_ring)
{
    // Send closed right after a burst: the tail keeps ringing.
    Engine e;
    prepare(e);
    e.setParameter(Param::SpaceHall, 100.0f);
    dryOnly(e);
    Buffer in = whiteNoise(2400, 3, 0.5f); // 50 ms burst
    in.resize(48000 * 3, 0.0f);
    const Stereo out = render(e, in, &in, fixedBlocks(64), [&](size_t pos) {
        if (pos == 2400)
            e.setParameter(Param::SpaceHall, 0.0f);
    });
    CHECK(rmsDb(out.l, 48000, 52800) > -60.0f); // 1 s later, still ringing
    CHECK(allFinite(out.l));
}

TEST(return_switches_cut_fast_and_keep_state)
{
    for (Param section : { Param::EchoEnabled, Param::SpaceEnabled, Param::SpacePlateEnabled }) {
        Engine e;
        prepare(e);
        // Only the effect behind the switch under test.
        if (section == Param::EchoEnabled) {
            e.setParameter(Param::EchoSend, 100.0f);
            e.setParameter(Param::EchoRepeats, 80.0f);
            e.setParameter(Param::EchoNote, 3.0f); // 1/16 at 120 BPM: a repeat every 125 ms
        } else {
            e.setParameter(Param::SpacePlate, 100.0f);
        }
        dryOnly(e);
        Buffer in(48000 * 2, 0.0f);
        std::copy_n(sine(300.0, 48000.0, 4800, 0.5f).begin(), 4800, in.begin()); // 100 ms tone
        const size_t off = 9600, on = 19200;
        const Stereo out = render(e, in, &in, fixedBlocks(64), [&](size_t pos) {
            if (pos == off)
                e.setParameter(section, 0.0f);
            if (pos == on)
                e.setParameter(section, 1.0f);
        });
        // Cut within the 5 ms fade (plus one block), and not before the switch.
        const size_t cut = off + 240 + 64;
        CHECK(rmsDb(out.l, off - 480, off) > -60.0f);
        const float residual = rmsDb(out.l, cut, on);
        CHECK(residual < -100.0f);
        // Switched back on: the tail continues where it is now, not from scratch.
        CHECK(rmsDb(out.l, on + 480, on + 2400) > -70.0f);
        CHECK(maxStep(out.l, off - 100, off + 600) < 0.05f);
    }
}

TEST(returns_duck_under_the_voice)
{
    // Sustained voice then silence.
    const double fs = 48000.0;
    Buffer in = vowel(fs, 48000 * 3);
    scaleToRmsDb(in, -18.0f);
    std::fill(in.begin() + 48000, in.end(), 0.0f);

    Engine e;
    prepare(e, fs);
    e.setParameter(Param::EchoSend, 100.0f);
    e.setParameter(Param::EchoRepeats, 70.0f);
    e.setParameter(Param::EchoNote, 6.0f); // 1/8 at 120 BPM = 250 ms
    dryOnly(e);
    const Stereo out = render(e, in, &in, fixedBlocks(64));
    CHECK(e.duckActivity() < 0.05f);

    // The same echo, without ducking.
    dsp::Echo echo;
    echo.prepare(fs);
    echo.setSend(1.0f);
    echo.setFeedback(0.7f);
    echo.setDelaySeconds(0.25);
    echo.reset();
    Stereo alone { Buffer(in.size()), Buffer(in.size()) };
    for (size_t pos = 0; pos < in.size(); pos += 64)
        echo.process(in.data() + pos, in.data() + pos, alone.l.data() + pos, alone.r.data() + pos, 64);

    Buffer returned(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        returned[i] = out.l[i] - in[i];
    const float duringVoice = rmsDb(returned, 24000, 48000) - rmsDb(alone.l, 24000, 48000);
    // 1 s after the voice stops the returns are back at full level.
    const float afterVoice = rmsDb(returned, 96000, 120000) - rmsDb(alone.l, 96000, 120000);
    CHECK_NEAR(duringVoice, -dsp::Ducker::kEchoDepthDb, 1.0);
    CHECK_NEAR(afterVoice, 0.0, 0.5);
}

TEST(tempo_changes_during_playback_are_click_free)
{
    // 60 -> 180 BPM while playing, extreme divisions, ping-pong, high feedback.
    for (float note : { 0.0f, 9.0f, 20.0f }) {
        Engine e;
        prepare(e);
        e.setParameter(Param::EchoSend, 100.0f);
        e.setParameter(Param::EchoRepeats, 90.0f);
        e.setParameter(Param::EchoBounce, 1.0f);
        e.setParameter(Param::EchoNote, note);
        dryOnly(e);
        const Buffer in = sine(220.0, 48000.0, 48000 * 6, 0.1f);
        size_t block = 0;
        const Stereo out = render(e, in, &in, fixedBlocks(64), [&](size_t) {
            e.setTempo(60.0 + 120.0 * std::min(1.0, static_cast<double>(block++) / 3000.0), true);
        });
        CHECK(allFinite(out.l) && allFinite(out.r));
        CHECK(peak(out.l) < 2.0f && peak(out.r) < 2.0f);
        CHECK(maxStep(out.l) < 0.05f && maxStep(out.r) < 0.05f);
    }
}

TEST(everything_on_is_finite_and_block_size_independent)
{
    auto run = [](double fs, const std::function<uint32_t()>& blocks) {
        Engine e;
        prepare(e, fs, 512);
        for (Param p : { Param::ColorSaturate, Param::ColorRadio, Param::ColorDouble, Param::ColorChorus,
                         Param::ColorDeess, Param::EchoSend, Param::EchoLofi, Param::SpaceRoom, Param::SpacePlate,
                         Param::SpaceHall, Param::SpaceAmbient })
            e.setParameter(p, 60.0f);
        e.setParameter(Param::EchoBounce, 1.0f);
        e.setParameter(Param::EchoRepeats, 70.0f);
        e.setParameter(Param::ToneAirDb, 6.0f);
        e.reset();
        const Buffer in = voiceLike(fs, 2.0, -12.0f);
        return render(e, in, nullptr, blocks);
    };
    for (double fs : kRates) {
        const Stereo out = run(fs, fixedBlocks(128));
        CHECK(allFinite(out.l) && allFinite(out.r));
        CHECK(peak(out.l) < 2.0f);
        CHECK(out.l != out.r); // mono in, stereo out
    }
    std::mt19937 rng(77);
    std::uniform_int_distribution<uint32_t> dist(1, 700);
    const Stereo ref = run(48000.0, fixedBlocks(64));
    const Stereo random = run(48000.0, [&] { return dist(rng); });
    CHECK(random.l == ref.l && random.r == ref.r);
}

TEST(effects_sleep_after_silence)
{
    Engine e;
    prepare(e);
    for (Param p : { Param::EchoSend, Param::SpaceRoom, Param::SpacePlate, Param::SpaceHall, Param::SpaceAmbient })
        e.setParameter(p, 100.0f);
    e.reset();
    Buffer in = voiceLike(48000.0, 1.0, -18.0f);
    render(e, in, &in, fixedBlocks(256));
    CHECK(!e.echoSleeping() && !e.reverbSleeping(3));
    const Buffer silence(48000 * 40, 0.0f);
    render(e, silence, &silence, fixedBlocks(256));
    CHECK(e.echoSleeping());
    for (uint32_t k = 0; k < Engine::kReverbCount; ++k)
        CHECK(e.reverbSleeping(k));
}

TEST(tail_length_follows_the_settings)
{
    Engine e;
    prepare(e);
    // Defaults: every reverb enabled (Ambient: 40 ms + 2 x 6 s), echo 1/4 at 120 BPM
    // with 25 % repeats (0.5 s x 9 = 4.5 s): the reverb dominates.
    CHECK_NEAR(e.tailSeconds(), 0.04 + 12.0, 1e-6);
    CHECK(e.tailSamples() == static_cast<uint32_t>(std::ceil((0.04 + 12.0) * 48000.0)));

    // A send at zero does not shorten it: a tail may still be ringing.
    e.setParameter(Param::SpaceAmbient, 0.0f);
    CHECK_NEAR(e.tailSeconds(), 12.04, 1e-6);

    e.setParameter(Param::SpaceAmbientEnabled, 0.0f);
    CHECK_NEAR(e.tailSeconds(), 0.024 + 2.0 * 2.8, 1e-6); // Hall next
    e.setParameter(Param::SpaceEnabled, 0.0f);
    CHECK_NEAR(e.tailSeconds(), 0.5 * 9 + 0.05, 1e-6);    // echo only

    // High feedback, long division, slow tempo: long but finite.
    e.setParameter(Param::EchoRepeats, 95.0f);
    e.setParameter(Param::EchoNote, 20.0f); // 2/1 dotted: 12 beats
    e.setTempo(40.0, true);                 // 18 s, clamped to 6 s
    CHECK(e.tailSeconds() > 1000.0 && e.tailSeconds() < 1300.0);
    CHECK(e.tailSamples() < 0xffffffffu);

    e.setParameter(Param::EchoEnabled, 0.0f);
    CHECK_NEAR(e.tailSeconds(), 0.1, 1e-9);
}

TEST(zero_latency_with_every_module_active)
{
    // The dry path must come out on the very sample it went in, whatever is enabled:
    // no lookahead, no internal buffering. Effects add delayed copies (echo, reverbs,
    // Double, Chorus) but never delay the voice itself.
    auto allOn = [](Engine& e, double fs) {
        prepare(e, fs, 512);
        e.setParameter(Param::CompressAmount, 100.0f);
        e.setParameter(Param::ToneBodyDb, 6.0f);
        e.setParameter(Param::TonePresenceDb, 6.0f);
        e.setParameter(Param::ToneAirDb, 9.0f);
        for (Param p : { Param::ColorDeess, Param::ColorSaturate, Param::ColorRadio, Param::ColorDouble,
                         Param::ColorChorus, Param::EchoSend, Param::EchoLofi, Param::SpaceRoom, Param::SpacePlate,
                         Param::SpaceHall, Param::SpaceAmbient })
            e.setParameter(p, 50.0f);
        e.setParameter(Param::EchoBounce, 1.0f);
        e.reset();
    };
    for (double fs : kRates) {
        // Impulse: sound comes out on the very first sample (a buffer or a lookahead
        // would leave it silent). The loudest sample may come a sample later: filters
        // such as a strong Air boost ring, and the limiter trims the first sample.
        Engine e;
        allOn(e, fs);
        Buffer impulse(static_cast<size_t>(fs * 0.5), 0.0f);
        impulse[0] = 0.5f;
        const Stereo out = render(e, impulse, &impulse, fixedBlocks(64));
        CHECK(std::fabs(out.l[0]) > 0.1f && std::fabs(out.r[0]) > 0.1f);

        // Noise: the output lines up with the input at lag 0, not a few samples later.
        Engine n;
        allOn(n, fs);
        const Buffer noise = whiteNoise(static_cast<size_t>(fs * 0.5), 21, 0.25f);
        const Stereo noisy = render(n, noise, &noise, fixedBlocks(64));
        int bestLag = -1;
        double best = -1.0;
        for (int lag = 0; lag <= 256; ++lag) {
            double c = 0.0;
            for (size_t i = 4096; i + static_cast<size_t>(lag) < noise.size(); ++i)
                c += static_cast<double>(noise[i]) * (noisy.l[i + static_cast<size_t>(lag)] + noisy.r[i + static_cast<size_t>(lag)]);
            if (c > best) {
                best = c;
                bestLag = lag;
            }
        }
        CHECK(bestLag == 0);
    }
    CHECK(Engine::latencySamples() == 0);
}
