// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.hpp"

#include "dsp/Denormals.hpp"
#include "dsp/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace titv {

namespace {
// In the order of Engine::reverbs_: Room, Plate, Hall, Ambient.
constexpr Param kReverbOn[Engine::kReverbCount] = { Param::SpaceRoomEnabled, Param::SpacePlateEnabled,
                                                    Param::SpaceHallEnabled, Param::SpaceAmbientEnabled };
constexpr Param kReverbSend[Engine::kReverbCount] = { Param::SpaceRoom, Param::SpacePlate, Param::SpaceHall,
                                                      Param::SpaceAmbient };
} // namespace

Engine::Engine()
{
    for (const ParamInfo& p : kParams)
        values_[index(p.id)].store(p.def, std::memory_order_relaxed);
}

void Engine::prepare(double sampleRate, uint32_t maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max<uint32_t>(maxBlockSize, 1);

    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        dry_[ch].assign(maxBlockSize_, 0.0f);
        wet_[ch].assign(maxBlockSize_, 0.0f);
        sectionDry_[ch].assign(maxBlockSize_, 0.0f);
        echoReturn_[ch].assign(maxBlockSize_, 0.0f);
        spaceReturn_[ch].assign(maxBlockSize_, 0.0f);
        reverbReturn_[ch].assign(maxBlockSize_, 0.0f);
    }
    presence_.assign(maxBlockSize_, 0.0f);
    reverbInput_.assign(maxBlockSize_, 0.0f);
    for (std::vector<float>* ramp : { &inGainRamp_, &outGainRamp_, &hpfRamp_, &processRamp_, &toneRamp_, &colorRamp_ })
        ramp->assign(maxBlockSize_, 0.0f);

    inputGain_.prepare(kGainSmoothingSeconds, sampleRate);
    outputGain_.prepare(kGainSmoothingSeconds, sampleRate);
    hpfMix_.prepare(kSwitchFadeSeconds, sampleRate);
    processMix_.prepare(kSwitchFadeSeconds, sampleRate);
    toneMix_.prepare(kSwitchFadeSeconds, sampleRate);
    colorMix_.prepare(kSwitchFadeSeconds, sampleRate);
    echoMix_.prepare(kReturnFadeSeconds, sampleRate);
    spaceMix_.prepare(kReturnFadeSeconds, sampleRate);
    for (dsp::Crossfade& m : reverbMix_)
        m.prepare(kReturnFadeSeconds, sampleRate);
    for (dsp::Smoother& send : reverbSend_)
        send.prepare(kSendSmoothingSeconds, sampleRate);
    slowRampSamplesLeft_ = 0;

    const auto inHpf = dsp::BiquadCoeffs::highPass(kInputHpfHz, kHpfQ, sampleRate);
    const auto outHpf = dsp::BiquadCoeffs::highPass(kOutputHpfHz, kHpfQ, sampleRate);
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        inputHpf_[ch].setCoeffs(inHpf);
        outputHpf_[ch].setCoeffs(outHpf);
    }

    tone_.prepare(sampleRate);
    compressor_.prepare(sampleRate);
    deEsser_.prepare(sampleRate);
    saturator_.prepare(sampleRate);
    radio_.prepare(sampleRate);
    doubler_.prepare(sampleRate);
    chorus_.prepare(sampleRate);
    ducker_.prepare(sampleRate);
    echo_.prepare(sampleRate);
    for (dsp::Reverb& rev : reverbs_)
        rev.prepare(sampleRate);
    autoLevel_.prepare(sampleRate);

    inputMeter_.prepare(sampleRate);
    outputMeter_.prepare(sampleRate);
    reset();
}

void Engine::reset() noexcept
{
    // Targets first: the modules snap their smoothers to them.
    snapSmoothers();
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        inputHpf_[ch].reset();
        outputHpf_[ch].reset();
    }
    tone_.reset();
    compressor_.reset();
    deEsser_.reset();
    saturator_.reset();
    radio_.reset();
    doubler_.reset();
    chorus_.reset();
    ducker_.reset();
    echo_.reset();
    for (dsp::Reverb& rev : reverbs_)
        rev.reset();
    inputMeter_.reset();
    outputMeter_.reset();
}

float Engine::clampToRange(Param p, float value) noexcept
{
    const ParamInfo& pi = info(p);
    if (!std::isfinite(value))
        return pi.def;
    value = std::clamp(value, pi.min, pi.max);
    switch (pi.kind) {
    case ParamKind::Toggle: return value >= 0.5f ? 1.0f : 0.0f;
    case ParamKind::Choice: return std::round(value);
    case ParamKind::Continuous: return value;
    }
    return value;
}

void Engine::setParameter(Param p, float value) noexcept
{
    if (index(p) >= kParamCount)
        return;
    values_[index(p)].store(clampToRange(p, value), std::memory_order_relaxed);
}

double Engine::tailSeconds() const noexcept
{
    // Modulation delays, filters and fades.
    double tail = 0.1;

    if (parameter(Param::SpaceEnabled) >= 0.5f) {
        for (uint32_t k = 0; k < kReverbCount; ++k) {
            // Independent of the send: a send just closed still leaves a tail ringing.
            // Twice RT60 covers -90 dB, with margin for the slower low end.
            if (parameter(kReverbOn[k]) >= 0.5f) {
                const dsp::Reverb::Design& d = reverbs_[k].design();
                tail = std::max(tail, d.predelayMs * 0.001 + 2.0 * d.rt60Seconds);
            }
        }
    }

    if (parameter(Param::EchoEnabled) >= 0.5f) {
        const double delay = std::min(noteDivisionSeconds(static_cast<uint32_t>(parameter(Param::EchoNote)), tempo()),
                                      dsp::Echo::kMaxDelaySeconds);
        const double gain = std::min(parameter(Param::EchoRepeats) / 100.0, 0.95);
        // Repeats until -90 dB: the first one at full level, then gain per repeat.
        const double repeats = gain > 0.0 ? 1.0 + std::ceil(90.0 / (-20.0 * std::log10(gain))) : 1.0;
        tail = std::max(tail, delay * repeats + dsp::Echo::kTimeFadeSeconds);
    }
    return tail;
}

uint32_t Engine::tailSamples() const noexcept
{
    const double samples = std::ceil(tailSeconds() * (sampleRate_ > 0.0 ? sampleRate_ : 48000.0));
    return samples >= 4294967294.0 ? 4294967294u : static_cast<uint32_t>(samples);
}

void Engine::setTempo(double bpm, bool valid) noexcept
{
    // Never extrapolate an old tempo once the host stops providing one.
    const bool usable = valid && std::isfinite(bpm) && bpm >= 1.0 && bpm <= 999.0;
    tempo_.store(usable ? bpm : kFallbackTempoBpm, std::memory_order_relaxed);
    tempoFallback_.store(!usable, std::memory_order_relaxed);
}

void Engine::updateTargets() noexcept
{
    inputGain_.setTarget(dsp::dbToGain(parameter(Param::InputGainDb)));
    outputGain_.setTarget(dsp::dbToGain(parameter(Param::OutputGainDb)));
    hpfMix_.setOn(parameter(Param::HpfEnabled) >= 0.5f);
    processMix_.setOn(parameter(Param::GlobalBypass) < 0.5f);
    toneMix_.setOn(parameter(Param::ToneEnabled) >= 0.5f);
    colorMix_.setOn(parameter(Param::ColorEnabled) >= 0.5f);
    tone_.setGains(parameter(Param::ToneBodyDb), parameter(Param::ToneMidDb), parameter(Param::TonePresenceDb),
                   parameter(Param::ToneAirDb));
    compressor_.setAmount(parameter(Param::CompressAmount) / 100.0f);
    deEsser_.setAmount(parameter(Param::ColorDeess) / 100.0f);
    saturator_.setAmount(parameter(Param::ColorSaturate) / 100.0f);
    radio_.setAmount(parameter(Param::ColorRadio) / 100.0f);
    doubler_.setAmount(parameter(Param::ColorDouble) / 100.0f);
    chorus_.setAmount(parameter(Param::ColorChorus) / 100.0f);

    echoMix_.setOn(parameter(Param::EchoEnabled) >= 0.5f);
    echo_.setSend(parameter(Param::EchoSend) / 100.0f);
    echo_.setFeedback(parameter(Param::EchoRepeats) / 100.0f);
    echo_.setLofi(parameter(Param::EchoLofi) / 100.0f);
    echo_.setBounce(parameter(Param::EchoBounce) >= 0.5f);
    echo_.setDelaySeconds(noteDivisionSeconds(static_cast<uint32_t>(parameter(Param::EchoNote)), tempo()));

    spaceMix_.setOn(parameter(Param::SpaceEnabled) >= 0.5f);
    for (uint32_t k = 0; k < kReverbCount; ++k) {
        reverbMix_[k].setOn(parameter(kReverbOn[k]) >= 0.5f);
        reverbSend_[k].setTarget(parameter(kReverbSend[k]) / 100.0f);
    }
}

void Engine::snapSmoothers() noexcept
{
    updateTargets();
    inputGain_.snap(inputGain_.target());
    outputGain_.snap(outputGain_.target());
    hpfMix_.snap(parameter(Param::HpfEnabled) >= 0.5f);
    processMix_.snap(parameter(Param::GlobalBypass) < 0.5f);
    toneMix_.snap(parameter(Param::ToneEnabled) >= 0.5f);
    colorMix_.snap(parameter(Param::ColorEnabled) >= 0.5f);
    echoMix_.snap(parameter(Param::EchoEnabled) >= 0.5f);
    spaceMix_.snap(parameter(Param::SpaceEnabled) >= 0.5f);
    for (uint32_t k = 0; k < kReverbCount; ++k) {
        reverbMix_[k].snap(parameter(kReverbOn[k]) >= 0.5f);
        reverbSend_[k].snap(reverbSend_[k].target());
    }
}

// ECHO and SPACE: sends taken after COLOR, returns ducked by the voice and summed
// with the dry path. Switching a section or a reverb off cuts its return
// over a few milliseconds and leaves its state running, so a tail is not lost.
void Engine::processReturns(uint32_t n) noexcept
{
    float* const wetL = wet_[0].data();
    float* const wetR = wet_[1].data();
    float* const echoL = echoReturn_[0].data();
    float* const echoR = echoReturn_[1].data();
    float* const spaceL = spaceReturn_[0].data();
    float* const spaceR = spaceReturn_[1].data();
    float* const revL = reverbReturn_[0].data();
    float* const revR = reverbReturn_[1].data();

    for (uint32_t i = 0; i < n; ++i)
        presence_[i] = ducker_.process(wetL[i], wetR[i]);
    ducker_.publish(presence_[n - 1]);

    echo_.process(wetL, wetR, echoL, echoR, n);

    std::fill(spaceL, spaceL + n, 0.0f);
    std::fill(spaceR, spaceR + n, 0.0f);
    bool spaceSilent = true;
    for (uint32_t k = 0; k < kReverbCount; ++k) {
        // Asleep with its send closed: nothing can come out of it.
        if (reverbs_[k].isSleeping() && reverbSend_[k].isSettled() && reverbSend_[k].target() == 0.0f) {
            reverbMix_[k].advance(n);
            continue;
        }
        spaceSilent = false;
        for (uint32_t i = 0; i < n; ++i)
            reverbInput_[i] = reverbSend_[k].next() * 0.5f * (wetL[i] + wetR[i]);
        std::fill(revL, revL + n, 0.0f);
        std::fill(revR, revR + n, 0.0f);
        reverbs_[k].process(reverbInput_.data(), revL, revR, n);
        for (uint32_t i = 0; i < n; ++i) {
            const float m = reverbMix_[k].next();
            spaceL[i] += m * revL[i];
            spaceR[i] += m * revR[i];
        }
    }

    if (echo_.isSleeping() && spaceSilent) {
        echoMix_.advance(n);
        spaceMix_.advance(n);
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        const float e = echoMix_.next() * dsp::Ducker::gain(presence_[i], dsp::Ducker::kEchoDepthDb);
        const float sp = spaceMix_.next() * dsp::Ducker::gain(presence_[i], dsp::Ducker::kSpaceDepthDb);
        wetL[i] += e * echoL[i] + sp * spaceL[i];
        wetR[i] += e * echoR[i] + sp * spaceR[i];
    }
}

// Runs a section with an on/off crossfade. The section always processes, so its
// state stays current and switching it back on is seamless.
template <typename Fn>
void Engine::runSection(const std::vector<float>& ramp, bool fullyOn, uint32_t n, Fn&& process) noexcept
{
    float* const wetL = wet_[0].data();
    float* const wetR = wet_[1].data();
    if (fullyOn) {
        process(wetL, wetR, n);
        return;
    }
    float* const dryL = sectionDry_[0].data();
    float* const dryR = sectionDry_[1].data();
    std::memcpy(dryL, wetL, n * sizeof(float));
    std::memcpy(dryR, wetR, n * sizeof(float));
    process(wetL, wetR, n);
    for (uint32_t i = 0; i < n; ++i) {
        wetL[i] = dryL[i] + ramp[i] * (wetL[i] - dryL[i]);
        wetR[i] = dryR[i] + ramp[i] * (wetR[i] - dryR[i]);
    }
}

void Engine::process(const float* const* inputs, uint32_t numInputs,
                     float* const* outputs, uint32_t numOutputs, uint32_t frames) noexcept
{
    // Parameter-only calls (VST3 may call process without buffers) still update targets.
    updateTargets();

    if (frames == 0 || outputs == nullptr || numOutputs == 0 || maxBlockSize_ == 0)
        return;
    if (inputs == nullptr)
        numInputs = 0;

    dsp::ScopedNoDenormals noDenormals;

    if (slowRampRequest_.exchange(false, std::memory_order_relaxed)) {
        inputGain_.prepare(kSlowGainSmoothingSeconds, sampleRate_);
        slowRampSamplesLeft_ = static_cast<uint32_t>(kSlowGainHoldSeconds * sampleRate_);
    }

    for (uint32_t offset = 0; offset < frames;) {
        const uint32_t n = std::min(frames - offset, maxBlockSize_);
        processChunk(inputs, numInputs, outputs, numOutputs, offset, n);
        offset += n;
    }

    if (slowRampSamplesLeft_ > 0) {
        slowRampSamplesLeft_ = frames >= slowRampSamplesLeft_ ? 0 : slowRampSamplesLeft_ - frames;
        if (slowRampSamplesLeft_ == 0)
            inputGain_.prepare(kGainSmoothingSeconds, sampleRate_);
    }
}

void Engine::processChunk(const float* const* inputs, uint32_t numInputs,
                          float* const* outputs, uint32_t numOutputs,
                          uint32_t offset, uint32_t n) noexcept
{
    float* const dryL = dry_[0].data();
    float* const dryR = dry_[1].data();
    float* const wetL = wet_[0].data();
    float* const wetR = wet_[1].data();

    // Copy the input first: the host may process in place.
    const float* inL = numInputs > 0 && inputs[0] != nullptr ? inputs[0] + offset : nullptr;
    const float* inR = numInputs > 1 && inputs[1] != nullptr ? inputs[1] + offset : inL;
    bool nonFinite = false;
    for (uint32_t i = 0; i < n; ++i) {
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : 0.0f;
        nonFinite |= !std::isfinite(l) || !std::isfinite(r);
        dryL[i] = dsp::sanitize(l);
        dryR[i] = dsp::sanitize(r);
    }
    // Auto Level measures the raw input; the gain it computes then applies to it.
    autoLevel_.process(dryL, dryR, n, nonFinite);

    // Parameter ramps, computed once per chunk and shared by both channels.
    bool toneOn = true, colorOn = true;
    for (uint32_t i = 0; i < n; ++i) {
        inGainRamp_[i] = inputGain_.next();
        outGainRamp_[i] = outputGain_.next();
        hpfRamp_[i] = hpfMix_.next();
        processRamp_[i] = processMix_.next();
        toneRamp_[i] = toneMix_.next();
        colorRamp_[i] = colorMix_.next();
        toneOn &= toneRamp_[i] == 1.0f;
        colorOn &= colorRamp_[i] == 1.0f;
    }

    // Input gain; the input meter reads right after it.
    for (uint32_t i = 0; i < n; ++i) {
        wetL[i] = dryL[i] * inGainRamp_[i];
        wetR[i] = dryR[i] * inGainRamp_[i];
    }
    inputMeter_.process(wetL, wetR, n);

    // Input HPF at 90 Hz. The filter always runs so that switching it on is seamless.
    for (uint32_t i = 0; i < n; ++i) {
        const float fl = inputHpf_[0].process(wetL[i]);
        const float fr = inputHpf_[1].process(wetR[i]);
        wetL[i] += hpfRamp_[i] * (fl - wetL[i]);
        wetR[i] += hpfRamp_[i] * (fr - wetR[i]);
    }

    // TONE: tonal colour and EQ.
    runSection(toneRamp_, toneOn, n, [this](float* l, float* r, uint32_t frames) { tone_.process(l, r, frames); });

    // COMPRESS has no section switch: 0 % is neutral.
    compressor_.process(wetL, wetR, n);

    // COLOR: de-esser first, then the creative effects.
    runSection(colorRamp_, colorOn, n, [this](float* l, float* r, uint32_t frames) {
        deEsser_.process(l, r, frames);
        saturator_.process(l, r, frames);
        radio_.process(l, r, frames);
        doubler_.process(l, r, frames);
        chorus_.process(l, r, frames);
    });

    processReturns(n);

    // Output HPF at 100 Hz on the sum, then Output gain.
    for (uint32_t i = 0; i < n; ++i) {
        const float fl = outputHpf_[0].process(wetL[i]);
        const float fr = outputHpf_[1].process(wetR[i]);
        wetL[i] = (wetL[i] + hpfRamp_[i] * (fl - wetL[i])) * outGainRamp_[i];
        wetR[i] = (wetR[i] + hpfRamp_[i] * (fr - wetR[i])) * outGainRamp_[i];
    }

    // Global bypass: short crossfade to the untouched input. Zero latency, so the dry
    // signal needs no compensation delay.
    for (uint32_t i = 0; i < n; ++i) {
        const float m = processRamp_[i];
        wetL[i] = dryL[i] + m * (wetL[i] - dryL[i]);
        wetR[i] = dryR[i] + m * (wetR[i] - dryR[i]);
    }
    outputMeter_.process(wetL, wetR, n);

    if (numOutputs == 1) {
        if (float* out = outputs[0])
            for (uint32_t i = 0; i < n; ++i)
                out[offset + i] = 0.5f * (wetL[i] + wetR[i]);
        return;
    }
    if (outputs[0] != nullptr)
        std::memcpy(outputs[0] + offset, wetL, n * sizeof(float));
    if (outputs[1] != nullptr)
        std::memcpy(outputs[1] + offset, wetR, n * sizeof(float));
    for (uint32_t ch = 2; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            std::memset(outputs[ch] + offset, 0, n * sizeof(float));
}

} // namespace titv
