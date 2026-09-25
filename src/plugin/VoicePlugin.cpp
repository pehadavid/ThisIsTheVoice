// SPDX-License-Identifier: GPL-3.0-or-later
// DPF adapter: maps the engine's parameter registry, state and audio callback onto
// DPF, which produces the VST3, CLAP, LV2 and JACK builds.

#include "DistrhoPlugin.hpp"
#include "VoicePlugin.hpp"

#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {
constexpr const char* kSchemaStateKey = "schema_version";
constexpr const char* kPresetStateKey = "preset";
} // namespace

VoicePlugin::VoicePlugin()
    : Plugin(titv::kParamCount, 0, 2)
{
    engine_.prepare(getSampleRate(), getBufferSize());
}

void VoicePlugin::initAudioPort(bool input, uint32_t index, AudioPort& port)
{
    port.groupId = kPortGroupStereo;
    Plugin::initAudioPort(input, index, port);
}

void VoicePlugin::initParameter(uint32_t index, Parameter& parameter)
{
    if (index >= titv::kParamCount)
        return;

    const titv::ParamInfo& pi = titv::kParams[index];
    parameter.symbol = pi.symbol;
    parameter.name = pi.name;
    parameter.shortName = pi.shortName;
    parameter.unit = pi.unit;
    parameter.description = pi.help;
    parameter.ranges.min = pi.min;
    parameter.ranges.max = pi.max;
    parameter.ranges.def = pi.def;
    parameter.hints = kParameterIsAutomatable;

    switch (pi.kind) {
    case titv::ParamKind::Toggle:
        parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        break;
    case titv::ParamKind::Choice: {
        parameter.hints |= kParameterIsInteger;
        auto* values = new ParameterEnumerationValue[titv::kNoteDivisionCount];
        for (uint32_t i = 0; i < titv::kNoteDivisionCount; ++i) {
            values[i].value = static_cast<float>(i);
            values[i].label = titv::kNoteDivisions[i].label;
        }
        parameter.enumValues.count = titv::kNoteDivisionCount;
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = values;
        break;
    }
    case titv::ParamKind::Continuous:
        break;
    }

    if (pi.id == titv::Param::GlobalBypass)
        parameter.designation = kParameterDesignationBypass;
}

void VoicePlugin::initState(uint32_t index, State& state)
{
    switch (index) {
    case 0:
        state.key = kSchemaStateKey;
        state.label = "State schema version";
        state.defaultValue = String(titv::kStateSchemaVersion);
        state.hints = kStateIsOnlyForDSP;
        break;
    case 1:
        // Id of the factory preset last chosen in the editor. The editor shows it, marked as modified when the values differ.
        state.key = kPresetStateKey;
        state.label = "Preset";
        state.defaultValue = titv::kPresets[0].id;
        state.hints = 0;
        break;
    default:
        break;
    }
}

float VoicePlugin::getParameterValue(uint32_t index) const
{
    return index < titv::kParamCount ? engine_.parameter(static_cast<titv::Param>(index)) : 0.0f;
}

void VoicePlugin::setParameterValue(uint32_t index, float value)
{
    if (index < titv::kParamCount)
        engine_.setParameter(static_cast<titv::Param>(index), value);
}

String VoicePlugin::getState(const char* key) const
{
    // Always saved with the current schema, whatever version was loaded.
    if (std::strcmp(key, kSchemaStateKey) == 0)
        return String(titv::kStateSchemaVersion);
    if (std::strcmp(key, kPresetStateKey) == 0)
        return presetId_;
    return String();
}

void VoicePlugin::setState(const char* key, const char* value)
{
    // Parameters are stored by symbol, so states from older versions simply lack the
    // parameters added since and keep their defaults. The loaded schema version is
    // kept for the conversions a future schema will need.
    if (std::strcmp(key, kSchemaStateKey) == 0)
        loadedSchemaVersion_ = std::atoi(value);
    else if (std::strcmp(key, kPresetStateKey) == 0)
        presetId_ = value; // main thread only, never read by the audio thread
}

void VoicePlugin::ioChanged(uint16_t numInputs, uint16_t numOutputs)
{
    // Called while deactivated; run() then receives arrays of exactly these sizes.
    numInputs_ = numInputs;
    numOutputs_ = numOutputs;
}

void VoicePlugin::activate()
{
    engine_.reset();
}

void VoicePlugin::sampleRateChanged(double newSampleRate)
{
    engine_.prepare(newSampleRate, getBufferSize());
}

void VoicePlugin::bufferSizeChanged(uint32_t newBufferSize)
{
    engine_.prepare(getSampleRate(), newBufferSize);
}

void VoicePlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    const TimePosition& pos = getTimePosition();
    engine_.setTempo(pos.bbt.beatsPerMinute, pos.bbt.valid);
    engine_.process(inputs, numInputs_, outputs, numOutputs_, frames);
}

Plugin* createPlugin()
{
    return new VoicePlugin();
}

END_NAMESPACE_DISTRHO
