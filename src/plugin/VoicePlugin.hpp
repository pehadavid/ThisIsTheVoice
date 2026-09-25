// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DistrhoPlugin.hpp"
#include "engine/Engine.hpp"
#include "engine/Presets.hpp"

START_NAMESPACE_DISTRHO

class VoicePlugin : public Plugin {
public:
    VoicePlugin();

    // Shared with the editor through DISTRHO_PLUGIN_WANT_DIRECT_ACCESS, for the meters only.
    titv::Engine& engine() noexcept { return engine_; }

protected:
    const char* getLabel() const override { return "ThisIsTheVoice"; }
    const char* getDescription() const override { return "Vocal chain: level, tone, compression, colour, echo and space."; }
    const char* getMaker() const override { return "pehadavid"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override
    {
        return d_version(TITV_VERSION_MAJOR, TITV_VERSION_MINOR, TITV_VERSION_PATCH);
    }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override;
    void initParameter(uint32_t index, Parameter& parameter) override;
    void initState(uint32_t index, State& state) override;

    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;
    String getState(const char* key) const override;
    void setState(const char* key, const char* value) override;

    uint32_t getTailSamples() const override { return engine_.tailSamples(); }

    void ioChanged(uint16_t numInputs, uint16_t numOutputs) override;
    void activate() override;
    void sampleRateChanged(double newSampleRate) override;
    void bufferSizeChanged(uint32_t newBufferSize) override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

private:
    titv::Engine engine_;
    int loadedSchemaVersion_ = titv::kStateSchemaVersion;
    // Channel layout negotiated with the host (AU may pick 1 in, 1 or 2 out).
    uint32_t numInputs_ = DISTRHO_PLUGIN_NUM_INPUTS;
    uint32_t numOutputs_ = DISTRHO_PLUGIN_NUM_OUTPUTS;
    String presetId_ { titv::kPresets[0].id };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePlugin)
};

END_NAMESPACE_DISTRHO
