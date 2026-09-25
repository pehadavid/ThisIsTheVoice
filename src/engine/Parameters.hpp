// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace titv {

// Parameter registry.
//
// The index of each entry is its host-visible ID in VST3 and CLAP, and the symbol
// is the key used in saved state. Both are frozen from the first release:
// entries are only ever APPENDED, never reordered, renamed or removed.
// tests/test_parameters.cpp holds the frozen list and fails on any reordering.
enum class Param : uint32_t {
    GlobalBypass,
    HpfEnabled,
    InputGainDb,
    OutputGainDb,
    CompressAmount,
    ToneEnabled,
    ToneBodyDb,
    ToneMidDb,
    TonePresenceDb,
    ToneAirDb,
    ColorEnabled,
    ColorDeess,
    ColorSaturate,
    ColorRadio,
    ColorDouble,
    ColorChorus,
    EchoEnabled,
    EchoSend,
    EchoRepeats,
    EchoLofi,
    EchoNote,
    EchoBounce,
    SpaceEnabled,
    SpaceRoomEnabled,
    SpacePlateEnabled,
    SpaceHallEnabled,
    SpaceAmbientEnabled,
    SpaceRoom,
    SpacePlate,
    SpaceHall,
    SpaceAmbient,
    Count
};

inline constexpr uint32_t kParamCount = static_cast<uint32_t>(Param::Count);

constexpr uint32_t index(Param p) noexcept { return static_cast<uint32_t>(p); }

enum class ParamKind : uint8_t { Continuous, Toggle, Choice };

struct ParamInfo {
    Param id;
    const char* symbol;     // stable state key, [a-z0-9_]
    const char* name;       // host display name
    const char* shortName;  // label on the editor
    const char* unit;
    ParamKind kind;
    float min, max, def;
    const char* help;       // one-sentence help naming the classic treatment (English, also sent to the host)
};

// Echo "Note" choices: seven straight values, each as
// straight, triplet (x 2/3) and dotted (x 3/2). Index = 3 * straight + variant.
inline constexpr uint32_t kNoteDivisionCount = 21;
inline constexpr uint32_t kNoteDivisionDefault = 9; // 1/4

struct NoteDivision {
    const char* label;
    double beats; // quarter notes
};

inline constexpr std::array<NoteDivision, kNoteDivisionCount> kNoteDivisions = { {
    { "1/32", 0.125 }, { "1/32T", 0.125 * 2.0 / 3.0 }, { "1/32D", 0.125 * 1.5 },
    { "1/16", 0.25 },  { "1/16T", 0.25 * 2.0 / 3.0 },  { "1/16D", 0.25 * 1.5 },
    { "1/8", 0.5 },    { "1/8T", 0.5 * 2.0 / 3.0 },    { "1/8D", 0.5 * 1.5 },
    { "1/4", 1.0 },    { "1/4T", 1.0 * 2.0 / 3.0 },    { "1/4D", 1.0 * 1.5 },
    { "1/2", 2.0 },    { "1/2T", 2.0 * 2.0 / 3.0 },    { "1/2D", 2.0 * 1.5 },
    { "1/1", 4.0 },    { "1/1T", 4.0 * 2.0 / 3.0 },    { "1/1D", 4.0 * 1.5 },
    { "2/1", 8.0 },    { "2/1T", 8.0 * 2.0 / 3.0 },    { "2/1D", 8.0 * 1.5 },
} };

inline constexpr double kFallbackTempoBpm = 120.0;

// Delay time in seconds for a division at a given tempo: 60 / BPM x beats.
constexpr double noteDivisionSeconds(uint32_t division, double bpm) noexcept
{
    return 60.0 / bpm * kNoteDivisions[division < kNoteDivisionCount ? division : kNoteDivisionDefault].beats;
}

using K = ParamKind;

inline constexpr std::array<ParamInfo, kParamCount> kParams = { {
    { Param::GlobalBypass, "global_bypass", "Bypass", "Bypass", "", K::Toggle, 0, 1, 0,
      "Bypass: passes the original signal through untouched." },
    { Param::HpfEnabled, "hpf_enabled", "HPF", "HPF", "", K::Toggle, 0, 1, 1,
      "HPF: high-pass filters that remove rumble below the voice, at the input and at the end of the chain." },
    { Param::InputGainDb, "input_gain_db", "Input", "Input", "dB", K::Continuous, -24, 24, 0,
      "Input: input gain, set it so the meter sits in the target zone." },
    { Param::OutputGainDb, "output_gain_db", "Output", "Output", "dB", K::Continuous, -24, 12, 0,
      "Output: overall output level, after the effects are mixed in." },
    { Param::CompressAmount, "compress_amount", "Compress", "COMPRESS", "%", K::Continuous, 0, 100, 70,
      "COMPRESS: parallel and serial compression, then a limiter, to keep the voice upfront." },
    { Param::ToneEnabled, "tone_enabled", "Tone", "TONE", "", K::Toggle, 0, 1, 1,
      "TONE: vocal equaliser." },
    { Param::ToneBodyDb, "tone_body_db", "Tone Body", "Body", "dB", K::Continuous, -15, 15, 0,
      "Body: low shelf, for the body of the voice." },
    { Param::ToneMidDb, "tone_mid_db", "Tone Mid", "Mid", "dB", K::Continuous, -15, 15, 0,
      "Mid: bell in the midrange." },
    { Param::TonePresenceDb, "tone_presence_db", "Tone Presence", "Presence", "dB", K::Continuous, -15, 15, 0,
      "Presence: upper-mid bell, for intelligibility." },
    { Param::ToneAirDb, "tone_air_db", "Tone Air", "Air", "dB", K::Continuous, -15, 15, 0,
      "Air: dynamic high boost that backs off on sibilance." },
    { Param::ColorEnabled, "color_enabled", "Color", "COLOR", "", K::Toggle, 0, 1, 1,
      "COLOR: colour effects." },
    { Param::ColorDeess, "color_deess", "De-Ess", "De-Ess", "%", K::Continuous, 0, 100, 0,
      "De-Ess: de-esser, reduces sibilance." },
    { Param::ColorSaturate, "color_saturate", "Saturate", "Saturate", "%", K::Continuous, 0, 100, 0,
      "Saturate: parallel harmonic saturation." },
    { Param::ColorRadio, "color_radio", "Radio", "Radio", "%", K::Continuous, 0, 100, 0,
      "Radio: telephone filter." },
    { Param::ColorDouble, "color_double", "Double", "Double", "%", K::Continuous, 0, 100, 0,
      "Double: stereo doubler made of slightly offset copies." },
    { Param::ColorChorus, "color_chorus", "Chorus", "Chorus", "%", K::Continuous, 0, 100, 0,
      "Chorus: chorus from modulated delays." },
    { Param::EchoEnabled, "echo_enabled", "Echo", "ECHO", "", K::Toggle, 0, 1, 1,
      "ECHO: tempo-synced delay." },
    { Param::EchoSend, "echo_send", "Echo Send", "Send", "%", K::Continuous, 0, 100, 0,
      "Send: level sent to the delay; the tail keeps ringing when you lower it." },
    { Param::EchoRepeats, "echo_repeats", "Echo Repeats", "Repeats", "%", K::Continuous, 0, 95, 25,
      "Repeats: delay feedback, from a single repeat to an almost endless tail." },
    { Param::EchoLofi, "echo_lofi", "Echo Lo-Fi", "Lo-Fi", "%", K::Continuous, 0, 100, 0,
      "Lo-Fi: telephone filter on the repeats only." },
    { Param::EchoNote, "echo_note", "Echo Note", "Note", "", K::Choice, 0, kNoteDivisionCount - 1, kNoteDivisionDefault,
      "Note: rhythmic division of the delay (T = triplet, D = dotted)." },
    { Param::EchoBounce, "echo_bounce", "Echo Bounce", "Bounce", "", K::Toggle, 0, 1, 0,
      "Bounce: ping-pong delay, repeats alternate left and right." },
    { Param::SpaceEnabled, "space_enabled", "Space", "SPACE", "", K::Toggle, 0, 1, 1,
      "SPACE: reverbs." },
    { Param::SpaceRoomEnabled, "space_room_enabled", "Room On", "Room", "", K::Toggle, 0, 1, 1,
      "Room: turns the room reverb on or off." },
    { Param::SpacePlateEnabled, "space_plate_enabled", "Plate On", "Plate", "", K::Toggle, 0, 1, 1,
      "Plate: turns the plate reverb on or off." },
    { Param::SpaceHallEnabled, "space_hall_enabled", "Hall On", "Hall", "", K::Toggle, 0, 1, 1,
      "Hall: turns the hall reverb on or off." },
    { Param::SpaceAmbientEnabled, "space_ambient_enabled", "Ambient On", "Ambient", "", K::Toggle, 0, 1, 1,
      "Ambient: turns the long ambient reverb on or off." },
    { Param::SpaceRoom, "space_room", "Room", "Room", "%", K::Continuous, 0, 100, 0,
      "Room: send to the room reverb." },
    { Param::SpacePlate, "space_plate", "Plate", "Plate", "%", K::Continuous, 0, 100, 0,
      "Plate: send to the plate reverb." },
    { Param::SpaceHall, "space_hall", "Hall", "Hall", "%", K::Continuous, 0, 100, 0,
      "Hall: send to the hall reverb." },
    { Param::SpaceAmbient, "space_ambient", "Ambient", "Ambient", "%", K::Continuous, 0, 100, 0,
      "Ambient: send to the long ambient reverb." },
} };

constexpr const ParamInfo& info(Param p) noexcept { return kParams[index(p)]; }

// Version of the saved-state schema. Bump it when the meaning of a stored value
// changes, and add the conversion from older versions in VoicePlugin::setState.
inline constexpr int kStateSchemaVersion = 1;

} // namespace titv
