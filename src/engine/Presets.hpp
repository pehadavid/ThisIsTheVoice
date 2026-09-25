// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Parameters.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace titv {

// Factory presets: original settings per use case, never named after
// artists or brands. A preset lists only the values that differ from the registry
// defaults. It never changes Input, which depends on the source level (Auto Level),
// nor the host bypass. Output is set so that every preset plays back at about the
// level of Init (within 0.5 dB on the test voice), for fair comparisons.
//
// The id is saved in the project state: never change an existing one.
struct Preset {
    const char* id;
    const char* nameEnglish;
    const char* nameFrench;
    std::vector<std::pair<Param, float>> values; // used off the audio thread only
};

inline constexpr bool presetControls(Param p) noexcept
{
    return p != Param::InputGainDb && p != Param::GlobalBypass;
}

inline const std::array<Preset, 7> kPresets = { {
    { "init", "Init", "Init", {} },

    { "natural_voice", "Natural Voice", "Voix naturelle",
      { { Param::CompressAmount, 45 }, { Param::ToneBodyDb, 1.0f }, { Param::TonePresenceDb, 1.5f },
        { Param::ToneAirDb, 2.0f }, { Param::ColorDeess, 30 }, { Param::ColorSaturate, 10 },
        { Param::SpaceRoom, 14 }, { Param::SpacePlate, 8 }, { Param::OutputGainDb, 0.0f } } },

    { "dense_lead", "Dense Lead", "Lead dense",
      { { Param::CompressAmount, 82 }, { Param::ToneBodyDb, 2.0f }, { Param::ToneMidDb, -1.5f },
        { Param::TonePresenceDb, 3.0f }, { Param::ToneAirDb, 4.5f }, { Param::ColorDeess, 45 },
        { Param::ColorSaturate, 30 }, { Param::ColorDouble, 12 }, { Param::EchoSend, 16 },
        { Param::EchoRepeats, 28 }, { Param::EchoNote, 8 /* 1/8D */ }, { Param::EchoLofi, 15 },
        { Param::SpacePlate, 22 }, { Param::SpaceHall, 12 }, { Param::OutputGainDb, 0.0f } } },

    { "wide_backing", "Wide Backing", "Back large",
      { { Param::CompressAmount, 70 }, { Param::ToneBodyDb, -3.0f }, { Param::ToneMidDb, -1.0f },
        { Param::TonePresenceDb, 1.0f }, { Param::ToneAirDb, 5.0f }, { Param::ColorDeess, 50 },
        { Param::ColorSaturate, 10 }, { Param::ColorDouble, 65 }, { Param::ColorChorus, 30 },
        { Param::EchoSend, 10 }, { Param::EchoRepeats, 20 }, { Param::SpaceRoom, 10 },
        { Param::SpaceHall, 26 }, { Param::OutputGainDb, 0.0f } } },

    { "spoken_word", "Spoken Word", "Voix parlée",
      { { Param::CompressAmount, 65 }, { Param::ToneBodyDb, 2.5f }, { Param::ToneMidDb, -1.0f },
        { Param::TonePresenceDb, 2.5f }, { Param::ToneAirDb, 1.0f }, { Param::ColorDeess, 40 },
        { Param::ColorSaturate, 8 }, { Param::SpaceRoom, 6 }, { Param::OutputGainDb, 0.0f } } },

    { "ad_lib", "Ad-Lib", "Ad-lib",
      { { Param::CompressAmount, 75 }, { Param::ToneBodyDb, -4.0f }, { Param::ToneMidDb, 3.0f },
        { Param::TonePresenceDb, 2.0f }, { Param::ColorDeess, 35 }, { Param::ColorSaturate, 35 },
        { Param::ColorRadio, 55 }, { Param::ColorDouble, 20 }, { Param::EchoSend, 35 },
        { Param::EchoRepeats, 45 }, { Param::EchoNote, 6 /* 1/8 */ }, { Param::EchoBounce, 1 },
        { Param::EchoLofi, 40 }, { Param::SpacePlate, 20 }, { Param::SpaceAmbient, 10 },
        { Param::OutputGainDb, 1.0f } } },

    { "ambience", "Ambience", "Ambiance",
      { { Param::CompressAmount, 60 }, { Param::TonePresenceDb, 1.0f }, { Param::ToneAirDb, 4.0f },
        { Param::ColorDeess, 35 }, { Param::ColorDouble, 30 }, { Param::ColorChorus, 40 },
        { Param::EchoSend, 30 }, { Param::EchoRepeats, 60 }, { Param::EchoNote, 11 /* 1/4D */ },
        { Param::EchoBounce, 1 }, { Param::EchoLofi, 20 }, { Param::SpaceHall, 30 },
        { Param::SpaceAmbient, 55 }, { Param::OutputGainDb, -0.5f } } },
} };

// Full parameter set of a preset: registry defaults, then the preset's values.
inline std::array<float, kParamCount> presetValues(const Preset& preset)
{
    std::array<float, kParamCount> v {};
    for (const ParamInfo& p : kParams)
        v[index(p.id)] = p.def;
    for (const auto& [param, value] : preset.values)
        v[index(param)] = value;
    return v;
}

inline int findPreset(const char* id)
{
    for (size_t i = 0; i < kPresets.size(); ++i) {
        const char* a = kPresets[i].id;
        const char* b = id;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == *b)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace titv
