// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Parameters.hpp"
#include "dsp/AutoLevel.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/ColorEffects.hpp"
#include "dsp/DeEsser.hpp"
#include "dsp/Ducker.hpp"
#include "dsp/Echo.hpp"
#include "dsp/LevelMeter.hpp"
#include "dsp/Reverb.hpp"
#include "dsp/Smoother.hpp"
#include "dsp/ToneEq.hpp"
#include "dsp/VoiceCompressor.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace titv {

// Audio engine shared by every plugin format and by the offline tools.
// It knows nothing of DPF, VST3 or CLAP.
//
// Threading: prepare() allocates and must be called outside the audio thread, while
// the engine is inactive. process(), reset() and setParameter() are real-time safe.
// Meters and parameter getters can be read from any thread.
class Engine {
public:
    static constexpr uint32_t kChannels = 2;
    static constexpr double kInputHpfHz = 90.0;
    static constexpr double kOutputHpfHz = 100.0;
    static constexpr double kHpfQ = 0.70710678118654752; // Butterworth, 12 dB/oct per filter
    static constexpr float kGainSmoothingSeconds = 0.020f;
    static constexpr float kSwitchFadeSeconds = 0.010f;
    // ECHO / SPACE switches cut the return at once, with a fade just long enough to avoid a click.
    static constexpr float kReturnFadeSeconds = 0.005f;
    static constexpr float kSendSmoothingSeconds = 0.020f;
    static constexpr uint32_t kReverbCount = 4;
    // Auto Level applies its result over a few hundred milliseconds.
    static constexpr float kSlowGainSmoothingSeconds = 0.080f;
    static constexpr float kSlowGainHoldSeconds = 0.600f;

    Engine();

    void prepare(double sampleRate, uint32_t maxBlockSize);
    void reset() noexcept;

    void setParameter(Param p, float value) noexcept;
    float parameter(Param p) const noexcept { return values_[index(p)].load(std::memory_order_relaxed); }

    // Host tempo for the current block; pass valid = false when the host provides none.
    void setTempo(double bpm, bool valid) noexcept;
    double tempo() const noexcept { return tempo_.load(std::memory_order_relaxed); }
    bool tempoIsFallback() const noexcept { return tempoFallback_.load(std::memory_order_relaxed); }

    // One mono input is spread to both channels; a mono output receives (L + R) / 2.
    // Output channels beyond the second are cleared. inputs and outputs may alias.
    void process(const float* const* inputs, uint32_t numInputs,
                 float* const* outputs, uint32_t numOutputs, uint32_t frames) noexcept;

    // The standard mode has no lookahead.
    static constexpr uint32_t latencySamples() noexcept { return 0; }

    // How long the output can keep sounding once the input is silent, from the current
    // settings: the longest enabled reverb and the echo's decay to -90 dB. Always
    // finite (Repeats stays below 1). Any thread.
    double tailSeconds() const noexcept;
    uint32_t tailSamples() const noexcept;

    double sampleRate() const noexcept { return sampleRate_; }
    uint32_t maxBlockSize() const noexcept { return maxBlockSize_; }

    dsp::LevelMeter& inputMeter() noexcept { return inputMeter_; }
    dsp::LevelMeter& outputMeter() noexcept { return outputMeter_; }
    dsp::AutoLevel& autoLevel() noexcept { return autoLevel_; }

    // Gain reduction for the editor's meters, in dB (>= 0).
    float compressorReductionDb() const noexcept { return compressor_.reductionDb(); }
    float deEsserReductionDb() const noexcept { return deEsser_.reductionDb(); }
    // Voice presence driving the ECHO / SPACE ducking (0..1).
    float duckActivity() const noexcept { return ducker_.activity(); }

    // Engines currently asleep (input silent and tail decayed), for tests and diagnostics.
    bool echoSleeping() const noexcept { return echo_.isSleeping(); }
    bool reverbSleeping(uint32_t i) const noexcept { return reverbs_[i].isSleeping(); }

    // The next Input changes, for a short while, ramp slowly instead of over 20 ms.
    // Used when Auto Level writes or reverts the Input gain. Any thread.
    void requestSlowInputRamp() noexcept { slowRampRequest_.store(true, std::memory_order_relaxed); }

    static float clampToRange(Param p, float value) noexcept;

private:
    void processChunk(const float* const* inputs, uint32_t numInputs,
                      float* const* outputs, uint32_t numOutputs,
                      uint32_t offset, uint32_t frames) noexcept;
    void updateTargets() noexcept;
    void snapSmoothers() noexcept;
    void processReturns(uint32_t frames) noexcept;
    template <typename Fn>
    void runSection(const std::vector<float>& ramp, bool fullyOn, uint32_t frames, Fn&& process) noexcept;

    std::array<std::atomic<float>, kParamCount> values_;
    std::atomic<double> tempo_ { kFallbackTempoBpm };
    std::atomic<bool> tempoFallback_ { true };

    double sampleRate_ = 0.0;
    uint32_t maxBlockSize_ = 0;

    dsp::Smoother inputGain_, outputGain_;
    dsp::Crossfade hpfMix_, processMix_; // processMix_ = 1 - bypass
    dsp::Crossfade toneMix_, colorMix_;
    dsp::Crossfade echoMix_, spaceMix_;
    std::array<dsp::Crossfade, kReverbCount> reverbMix_;
    std::array<dsp::Smoother, kReverbCount> reverbSend_;
    std::atomic<bool> slowRampRequest_ { false };
    uint32_t slowRampSamplesLeft_ = 0;

    std::array<dsp::Biquad, kChannels> inputHpf_, outputHpf_;
    dsp::ToneEq tone_;
    dsp::VoiceCompressor compressor_;
    dsp::DeEsser deEsser_;
    dsp::Saturator saturator_;
    dsp::Radio radio_;
    dsp::Doubler doubler_;
    dsp::Chorus chorus_;
    dsp::Ducker ducker_;
    dsp::Echo echo_;
    std::array<dsp::Reverb, kReverbCount> reverbs_ { dsp::Reverb(dsp::Reverb::kRoom), dsp::Reverb(dsp::Reverb::kPlate),
                                                     dsp::Reverb(dsp::Reverb::kHall), dsp::Reverb(dsp::Reverb::kAmbient) };
    dsp::AutoLevel autoLevel_;

    // Per-chunk scratch, sized in prepare().
    std::array<std::vector<float>, kChannels> dry_, wet_, sectionDry_, echoReturn_, spaceReturn_, reverbReturn_;
    std::vector<float> inGainRamp_, outGainRamp_, hpfRamp_, processRamp_, toneRamp_, colorRamp_;
    std::vector<float> presence_, reverbInput_;

    dsp::LevelMeter inputMeter_, outputMeter_;
};

} // namespace titv
