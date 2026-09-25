// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the measured characteristics of the DSP modules as Markdown. Usage:
//   titv_measure > private/measurements.md

#include "EngineHelpers.hpp"

#include "dsp/ColorEffects.hpp"
#include "dsp/DeEsser.hpp"
#include "dsp/Reverb.hpp"
#include "dsp/ToneEq.hpp"
#include "dsp/VoiceCompressor.hpp"

#include <cstdio>
#include <iterator>

using namespace titv;
using namespace titv::dsp;
using namespace titv::test;

namespace {

constexpr double kFs = 48000.0;
constexpr double kFreqs[] = { 50, 90, 100, 120, 180, 280, 500, 1000, 1200, 2000, 2800, 4500, 7000, 9000, 12000, 16000 };

double db(double x) { return 20.0 * std::log10(x); }

// Adding zero turns -0.0 into +0.0, so tables never print "-0.0".
double z(double x) { return x + 0.0; }

template <typename Module>
Buffer run(Module& m, const Buffer& in, const std::function<void()>& onBlock = {})
{
    Buffer l = in, r = in;
    for (size_t pos = 0; pos < in.size(); pos += 64) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(64, in.size() - pos));
        m.process(l.data() + pos, r.data() + pos, n);
        if (onBlock)
            onBlock();
    }
    return l;
}

void header(const char* const* cols, size_t n)
{
    std::printf("|");
    for (size_t i = 0; i < n; ++i)
        std::printf(" %s |", cols[i]);
    std::printf("\n|");
    for (size_t i = 0; i < n; ++i)
        std::printf(" ---: |");
    std::printf("\n");
}

void frequencyResponses()
{
    std::printf("## Frequency responses (48 kHz)\n\n");
    std::printf("Computed from the coefficients used by the engine, in dB. "
                "The tests `hpf_cascade_matches_design`, `tone_bands_match_design` and "
                "`tonal_color_curve_response` check that the rendered output matches within ±0.05 dB.\n\n");

    const auto hpfIn = BiquadCoeffs::highPass(Engine::kInputHpfHz, Engine::kHpfQ, kFs);
    const auto hpfOut = BiquadCoeffs::highPass(Engine::kOutputHpfHz, Engine::kHpfQ, kFs);
    const char* cols[] = { "Hz", "HPF (90 + 100 Hz)", "Tonal colour", "Body +15", "Mid +15", "Presence +15", "Air +15 (static)" };
    header(cols, std::size(cols));
    for (double f : kFreqs) {
        double color = 0.0;
        for (const auto& b : ToneEq::kColorCurve)
            color += db(ToneEq::colorCoeffs(b, kFs).magnitudeAt(f, kFs));
        std::printf("| %.0f | %.2f | %+.2f | %+.2f | %+.2f | %+.2f | %+.2f |\n", f,
                    db(hpfIn.magnitudeAt(f, kFs) * hpfOut.magnitudeAt(f, kFs)), color,
                    db(BiquadCoeffs::lowShelf(ToneEq::kBody.freq, ToneEq::kBody.q, 15, kFs).magnitudeAt(f, kFs)),
                    db(BiquadCoeffs::peaking(ToneEq::kMid.freq, ToneEq::kMid.q, 15, kFs).magnitudeAt(f, kFs)),
                    db(BiquadCoeffs::peaking(ToneEq::kPresence.freq, ToneEq::kPresence.q, 15, kFs).magnitudeAt(f, kFs)),
                    db(BiquadCoeffs::highShelf(ToneEq::kAir.freq, ToneEq::kAir.q, 15, kFs).magnitudeAt(f, kFs)));
    }
    std::printf("\nTonal colour: ");
    for (const auto& b : ToneEq::kColorCurve)
        std::printf("%s %.0f Hz %+.1f dB (Q %.1f)%s", b.type == ToneEq::ColorBand::HighShelf ? "high shelf" : "bell",
                    b.freq, b.gainDb, b.q, &b == &ToneEq::kColorCurve.back() ? ".\n\n" : ", ");
}

void airDynamics()
{
    std::printf("## Dynamic Air\n\n");
    std::printf("Air set to +9 dB; gain actually applied (minimum over 1 s) depending on the signal (−20 dBFS RMS).\n\n");
    const char* cols[] = { "Signal", "Air applied (dB)" };
    header(cols, 2);
    Buffer v = vowel(kFs, 48000);
    scaleToRmsDb(v, -20.0f);
    Buffer s = sibilantNoise(kFs, 48000);
    scaleToRmsDb(s, -20.0f);
    const Buffer voice = voiceLike(kFs, 1.0, -20.0f);
    const std::pair<const char*, const Buffer*> signals[] = { { "vowel (harmonics of 180 Hz)", &v },
                                                                { "sibilant (5-10 kHz noise)", &s },
                                                                { "test voice (syllables + sibilants)", &voice } };
    for (const auto& [name, buf] : signals) {
        ToneEq eq;
        eq.prepare(kFs);
        eq.setGains(0, 0, 0, 9.0f);
        eq.reset();
        float minApplied = 100.0f;
        size_t blocks = 0;
        run(eq, *buf, [&] {
            if (++blocks > 200)
                minApplied = std::min(minApplied, eq.airAppliedDb());
        });
        std::printf("| %s | %+.2f |\n", name, minApplied);
    }
    std::printf("\n");
}

void compressor()
{
    std::printf("## COMPRESS\n\n");
    std::printf("Settings of the three stages versus the macro (thresholds in dBFS; stage 1 on RMS, stage 2 on the peak "
                "envelope; limiter at %.1f dBFS, then soft clip above %.2f).\n\n",
                VoiceCompressor::kLimiterCeilingDb, VoiceCompressor::kClipKnee);
    const char* cols[] = { "Macro", "Parallel: threshold / ratio / mix / make-up", "Serial: threshold / ratio / make-up", "Limiter" };
    header(cols, std::size(cols));
    for (float a : { 0.0f, 0.25f, 0.5f, 0.7f, 1.0f }) {
        const auto st = VoiceCompressor::settingsFor(a);
        std::printf("| %.0f %% | %.0f / %.2f:1 / %.0f %% / %+.1f dB | %.1f / %.1f:1 / %+.1f dB | %.0f %% |\n", a * 100,
                    st.parallelThresholdDb, st.parallelRatio, st.parallelMix * 100, z(VoiceCompressor::parallelMakeupDb(st)),
                    st.serialThresholdDb, st.serialRatio, z(VoiceCompressor::serialMakeupDb(st)), st.limiterMix * 100);
    }

    std::printf("\nStatic curve of the serial stage (gain in dB, make-up included):\n\n");
    const char* cols2[] = { "Peak level (dBFS)", "25 %", "50 %", "70 %", "100 %" };
    header(cols2, std::size(cols2));
    for (float level : { -40.0f, -30.0f, -24.0f, -18.0f, -13.0f, -9.0f, -6.0f, -3.0f, 0.0f }) {
        std::printf("| %.0f |", level);
        for (float a : { 0.25f, 0.5f, 0.7f, 1.0f }) {
            const auto st = VoiceCompressor::settingsFor(a);
            std::printf(" %+.1f |", z(compressorGainDb(level, st.serialThresholdDb, st.serialRatio, VoiceCompressor::kSerialKneeDb) +
                                          VoiceCompressor::serialMakeupDb(st)));
        }
        std::printf("\n");
    }

    std::printf("\nMeasured on the test voice (6 s; phrases alternating 18 dB apart for the dynamics):\n\n");
    const char* cols3[] = { "Macro", "Output level change at −18 dBFS RMS", "Loud/quiet spread", "Output peak (input +12 dBFS)" };
    header(cols3, std::size(cols3));
    const Buffer in = voiceLike(kFs, 6.0, -18.0f);
    Buffer alternating = voiceLike(kFs, 8.0, -18.0f);
    for (size_t i = 0; i < alternating.size(); ++i)
        if ((i / 48000) % 2 == 1)
            alternating[i] *= dbToGain(-18.0f);
    const Buffer hot = sine(200.0, kFs, 48000, 4.0f);
    for (float a : { 0.0f, 0.25f, 0.5f, 0.7f, 1.0f }) {
        auto make = [&](VoiceCompressor& c) {
            c.prepare(kFs);
            c.setAmount(a);
            c.reset();
        };
        VoiceCompressor c1, c2, c3;
        make(c1);
        make(c2);
        make(c3);
        const Buffer o1 = run(c1, in);
        const Buffer o2 = run(c2, alternating);
        const Buffer o3 = run(c3, hot);
        std::printf("| %.0f %% | %+.2f dB | %.1f dB | %.3f (%+.2f dBFS) |\n", a * 100, z(rmsDb(o1, 48000) - rmsDb(in, 48000)),
                    rmsDb(o2, 2 * 48000 + 9600, 3 * 48000) - rmsDb(o2, 3 * 48000 + 9600, 4 * 48000), peak(o3),
                    z(db(peak(o3))));
    }
    std::printf("\n");
}

void deEsser()
{
    std::printf("## De-Ess\n\n");
    std::printf("Relative detection (%.0f Hz band after a %.0f Hz high-pass, compared with the full band), cutting bell at %.0f Hz. "
                "Maximum reduction measured over 1 s of signal at −20 dBFS RMS.\n\n",
                DeEsser::kFrequencyHz, DeEsser::kDetectorHighPassHz, DeEsser::kFrequencyHz);
    const char* cols[] = { "Setting", "Relative threshold", "Maximum reduction allowed", "Sibilant", "Vowel" };
    header(cols, std::size(cols));
    Buffer v = vowel(kFs, 48000);
    scaleToRmsDb(v, -20.0f);
    Buffer s = sibilantNoise(kFs, 48000);
    scaleToRmsDb(s, -20.0f);
    for (float a : { 0.25f, 0.5f, 0.75f, 1.0f }) {
        auto measure = [&](const Buffer& b) {
            DeEsser d;
            d.prepare(kFs);
            d.setAmount(a);
            d.reset();
            float m = 0.0f;
            run(d, b, [&] { m = std::max(m, d.reductionDb()); });
            return m;
        };
        std::printf("| %.0f %% | %.1f dB | %.1f dB | %.1f dB | %.1f dB |\n", a * 100, DeEsser::thresholdDb(a),
                    DeEsser::maxReductionDb(a), measure(s), measure(v));
    }
    std::printf("\n");
}

// Level of one frequency (Goertzel), in dB relative to a full-scale sine.
double toneDb(const Buffer& b, double freq, size_t from)
{
    const double w = 2.0 * kPi * freq / kFs, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < b.size(); ++i) {
        const double v = b[i] + c * s1 - s2;
        s2 = s1;
        s1 = v;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10.0 * std::log10(std::max(power, 1e-30)) - 20.0 * std::log10(static_cast<double>(b.size() - from) / 2.0);
}

template <typename Module>
Buffer runAt(Module& m, float amount, const Buffer& in, Buffer* right = nullptr)
{
    m.prepare(kFs);
    m.setAmount(amount);
    m.reset();
    Buffer l = in, r = in;
    for (size_t pos = 0; pos < in.size(); pos += 64) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(64, in.size() - pos));
        m.process(l.data() + pos, r.data() + pos, n);
    }
    if (right != nullptr)
        *right = r;
    return l;
}

void colorEffects()
{
    std::printf("## COLOR: creative effects\n\n");
    std::printf("### Saturate\n\n");
    std::printf("Parallel path: a copy high-passed at %.0f Hz is driven up to +%.0f dB into a hyperbolic tangent with "
                "antiderivative anti-aliasing (ADAA); only the difference from the clean copy (harmonics and rounded "
                "peaks) is low-passed at %.0f Hz and added. Distortion measured on a sine at −12 dBFS; aliasing: the "
                "13 kHz component (5th harmonic of 7 kHz, folded back) of a 7 kHz sine at −6 dBFS, compared with a "
                "saturation without ADAA at the same setting.\n\n",
                Saturator::kPreHighPassHz, Saturator::kMaxDriveDb, Saturator::kPostLowPassHz);
    const char* cols[] = { "Setting", "H3 at 500 Hz (dB re fund.)", "H2+H3 at 2 kHz (dB re fund.)", "13 kHz alias, ADAA", "Without ADAA", "Voice level at −18 dBFS" };
    header(cols, std::size(cols));
    const Buffer s500 = sine(500.0, kFs, 48000, dbToGain(-12.0f));
    const Buffer s2k = sine(2000.0, kFs, 48000, dbToGain(-12.0f));
    const Buffer s7k = sine(7000.0, kFs, 48000, dbToGain(-6.0f));
    const Buffer voice = voiceLike(kFs, 4.0, -18.0f);
    for (float a : { 0.25f, 0.5f, 0.75f, 1.0f }) {
        Saturator sat;
        const Buffer o500 = runAt(sat, a, s500);
        const Buffer o2k = runAt(sat, a, s2k);
        const Buffer o7k = runAt(sat, a, s7k);
        const Buffer ov = runAt(sat, a, voice);
        // Reference without ADAA: same drive, plain tanh, same mix and filters omitted.
        const float drive = dbToGain(Saturator::kMaxDriveDb * a);
        Buffer naive(s7k.size());
        for (size_t i = 0; i < s7k.size(); ++i)
            naive[i] = s7k[i] + a * Saturator::kMakeup * (std::tanh(drive * s7k[i]) / drive - s7k[i]);
        const double f500 = toneDb(o500, 500, 4800), f2k = toneDb(o2k, 2000, 4800), f7k = toneDb(o7k, 7000, 4800);
        const double h23 = 10.0 * std::log10(std::pow(10.0, toneDb(o2k, 4000, 4800) / 10.0) + std::pow(10.0, toneDb(o2k, 6000, 4800) / 10.0));
        std::printf("| %.0f %% | %.1f | %.1f | %.1f dB | %.1f dB | %+.2f dB |\n", a * 100, toneDb(o500, 1500, 4800) - f500,
                    h23 - f2k, toneDb(o7k, 13000, 4800) - f7k, toneDb(naive, 13000, 4800) - toneDb(naive, 7000, 4800),
                    z(rmsDb(ov, 48000) - rmsDb(voice, 48000)));
    }

    std::printf("\n### Radio\n\nResponse at 100 %% (dB, level make-up included):\n\n");
    const char* cols2[] = { "Hz", "100", "250", "500", "1000", "1700", "3000", "5000", "10000" };
    header(cols2, std::size(cols2));
    std::printf("| gain |");
    for (double f : { 100.0, 250.0, 500.0, 1000.0, 1700.0, 3000.0, 5000.0, 10000.0 }) {
        Radio radio;
        const Buffer in = sine(f, kFs, 24000, 0.1f);
        std::printf(" %+.1f |", z(rmsDb(runAt(radio, 1.0f, in), 12000) - rmsDb(in, 12000)));
    }
    std::printf("\n\n### Double and Chorus\n\nTest voice at −18 dBFS RMS, 4 s.\n\n");
    const char* cols3[] = { "Effect (100 %)", "Left level", "Mono sum", "L/R correlation" };
    header(cols3, std::size(cols3));
    auto stereoStats = [&](const char* name, auto& m) {
        Buffer r;
        const Buffer l = runAt(m, 1.0f, voice, &r);
        Buffer mono(l.size());
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < l.size(); ++i) {
            mono[i] = 0.5f * (l[i] + r[i]);
            lr += static_cast<double>(l[i]) * r[i];
            ll += static_cast<double>(l[i]) * l[i];
            rr += static_cast<double>(r[i]) * r[i];
        }
        std::printf("| %s | %+.2f dB | %+.2f dB | %.2f |\n", name, rmsDb(l, 48000) - rmsDb(voice, 48000),
                    rmsDb(mono, 48000) - rmsDb(voice, 48000), lr / std::sqrt(ll * rr));
    };
    Doubler dbl;
    Chorus chorus;
    stereoStats("Double", dbl);
    stereoStats("Chorus", chorus);
    std::printf("\n");
}

void reverbs()
{
    std::printf("## SPACE: reverbs\n\n");
    std::printf("Feedback delay networks (Householder matrix), with the in-loop damping compensated at 1 kHz so the "
                "stated RT60 is the mid-band one. Return measured on the test voice at −18 dBFS RMS with the send at "
                "100 %%, relative to the dry voice; RT60 measured on the decay of a noise burst (slope between 0.1 s "
                "and 0.1 s + RT60/3).\n\n");
    const char* cols[] = { "Engine", "Lines", "Predelay", "Design RT60", "Measured RT60", "Return at 100 %", "L/R correlation", "Asleep after" };
    header(cols, std::size(cols));
    const Buffer voice = voiceLike(kFs, 4.0, -18.0f);
    for (const Reverb::Design* d : { &Reverb::kRoom, &Reverb::kPlate, &Reverb::kHall, &Reverb::kAmbient }) {
        auto render = [&](const Buffer& in, Buffer& l, Buffer& r, size_t& sleptAt) {
            Reverb rev(*d);
            rev.prepare(kFs);
            l.assign(in.size(), 0.0f);
            r.assign(in.size(), 0.0f);
            sleptAt = 0;
            for (size_t pos = 0; pos < in.size(); pos += 64) {
                rev.process(in.data() + pos, l.data() + pos, r.data() + pos, 64);
                if (sleptAt == 0 && pos > 24000 && rev.isSleeping())
                    sleptAt = pos;
            }
        };
        Buffer l, r;
        size_t slept;
        render(voice, l, r, slept);
        const float wet = rmsDb(l, 48000) - rmsDb(voice, 48000);
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < l.size(); ++i) {
            lr += static_cast<double>(l[i]) * r[i];
            ll += static_cast<double>(l[i]) * l[i];
            rr += static_cast<double>(r[i]) * r[i];
        }
        const size_t burst = 24000;
        Buffer noise = whiteNoise(burst, 9, 0.5f);
        noise.resize(burst + static_cast<size_t>((d->rt60Seconds * 4.0 + 2.0) * kFs), 0.0f);
        render(noise, l, r, slept);
        const size_t a = burst + 4800, b = a + static_cast<size_t>(d->rt60Seconds / 3.0 * kFs);
        const double rt60 = 60.0 * (d->rt60Seconds / 3.0) / (rmsDb(l, a, a + 2400) - rmsDb(l, b, b + 2400));
        std::printf("| %s | %u | %.0f ms | %.1f s | %.1f s | %+.1f dB | %.2f | %.1f s |\n", d->name, d->lines, d->predelayMs,
                    d->rt60Seconds, rt60, wet, lr / std::sqrt(ll * rr), slept > 0 ? static_cast<double>(slept - burst) / kFs : -1.0);
    }
    std::printf("\nRoom is mostly early reflections (12 echoes between 7 and 68 ms); its tail RT60 does not sum up "
                "its perceived length.\n\n");
}

} // namespace

int main()
{
    std::printf("# Measurements\n\n");
    std::printf("Generated by `build/tests/titv_measure > private/measurements.md`. The test signals are synthetic "
                "(`tests/EngineHelpers.hpp`); they do not replace listening on real vocal recordings.\n\n");
    frequencyResponses();
    airDynamics();
    compressor();
    deEsser();
    colorEffects();
    reverbs();
    return 0;
}
