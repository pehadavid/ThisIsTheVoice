// SPDX-License-Identifier: GPL-3.0-or-later
// Processing-time benchmark: time per process() call as a share of the
// block duration, for one instance. Usage: titv_bench [seconds-per-config]

#include "engine/Engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

struct Stats {
    double median, p99, max;
};

Stats measure(double sampleRate, uint32_t block, double seconds)
{
    titv::Engine engine;
    engine.prepare(sampleRate, block);
    // Every implemented module active, EQ bands away from 0 dB so that their
    // coefficients are live.
    using titv::Param;
    engine.setParameter(Param::ToneBodyDb, 3.0f);
    engine.setParameter(Param::ToneMidDb, -2.0f);
    engine.setParameter(Param::TonePresenceDb, 2.0f);
    engine.setParameter(Param::ToneAirDb, 6.0f);
    engine.setParameter(Param::ColorDeess, 60.0f);
    engine.setParameter(Param::CompressAmount, 100.0f);
    for (Param p : { Param::ColorSaturate, Param::ColorRadio, Param::ColorDouble, Param::ColorChorus, Param::EchoSend,
                     Param::EchoLofi, Param::SpaceRoom, Param::SpacePlate, Param::SpaceHall, Param::SpaceAmbient })
        engine.setParameter(p, 50.0f);
    engine.setParameter(Param::EchoBounce, 1.0f);
    engine.reset();

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    std::vector<float> l(block), r(block);
    for (uint32_t i = 0; i < block; ++i) {
        l[i] = noise(rng);
        r[i] = noise(rng);
    }
    const float* ins[2] = { l.data(), r.data() };
    std::vector<float> ol(block), orr(block);
    float* outs[2] = { ol.data(), orr.data() };

    const size_t calls = static_cast<size_t>(seconds * sampleRate / block);
    std::vector<double> times;
    times.reserve(calls);

    for (size_t i = 0; i < 200; ++i) // warm-up
        engine.process(ins, 2, outs, 2, block);

    for (size_t i = 0; i < calls; ++i) {
        // Keep the smoothers moving, as under automation.
        engine.setParameter(titv::Param::InputGainDb, (i / 16) % 2 == 0 ? 6.0f : -6.0f);
        const auto t0 = std::chrono::steady_clock::now();
        engine.process(ins, 2, outs, 2, block);
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());
    const double blockSeconds = block / sampleRate;
    return { times[times.size() / 2] / blockSeconds * 100.0,
             times[times.size() * 99 / 100] / blockSeconds * 100.0,
             times.back() / blockSeconds * 100.0 };
}

} // namespace

int main(int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 20.0;
    std::printf("Share of block time used by process() (one instance, all implemented modules active, %.0f s of audio per row)\n\n", seconds);
    std::printf("%8s %6s %10s %10s %10s\n", "rate", "block", "median %", "p99 %", "max %");

    bool withinBudget = true;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (uint32_t block : { 32u, 64u, 128u, 512u, 2048u }) {
            const Stats s = measure(fs, block, seconds);
            std::printf("%8.0f %6u %10.3f %10.3f %10.3f\n", fs, block, s.median, s.p99, s.max);
            if (fs == 48000.0 && block == 64)
                withinBudget = s.median <= 5.0 && s.p99 <= 15.0 && s.max <= 50.0;
        }
    }
    std::printf("\n48 kHz / 64: %s (budget: median <= 5 %%, p99 <= 15 %%, max <= 50 %%)\n",
                withinBudget ? "within budget" : "OVER BUDGET");
    return withinBudget ? 0 : 1;
}
