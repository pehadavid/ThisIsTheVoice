// SPDX-License-Identifier: MIT
#include "TestHarness.hpp"

#include "engine/Engine.hpp"

#include <cstring>
#include <iterator>
#include <set>
#include <string>

using namespace titv;

// Frozen registry: the host IDs are the indices below. Only append to this list,
// together with Param and kParams; never edit or reorder existing entries.
TEST(registry_is_frozen)
{
    static const char* const kFrozen[] = {
        "global_bypass", "hpf_enabled", "input_gain_db", "output_gain_db", "compress_amount",
        "tone_enabled", "tone_body_db", "tone_mid_db", "tone_presence_db", "tone_air_db",
        "color_enabled", "color_deess", "color_saturate", "color_radio", "color_double", "color_chorus",
        "echo_enabled", "echo_send", "echo_repeats", "echo_lofi", "echo_note", "echo_bounce",
        "space_enabled", "space_room_enabled", "space_plate_enabled", "space_hall_enabled",
        "space_ambient_enabled", "space_room", "space_plate", "space_hall", "space_ambient",
        "voice_register",
    };
    CHECK(kParamCount >= std::size(kFrozen));
    for (size_t i = 0; i < std::size(kFrozen) && i < kParamCount; ++i)
        CHECK(std::strcmp(kParams[i].symbol, kFrozen[i]) == 0);
}

TEST(registry_entries_are_consistent)
{
    std::set<std::string> symbols;
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const ParamInfo& p = kParams[i];
        CHECK(index(p.id) == i);
        CHECK(p.min < p.max);
        CHECK(p.def >= p.min && p.def <= p.max);
        CHECK(std::strlen(p.help) > 0);
        CHECK(symbols.insert(p.symbol).second);
        for (const char* c = p.symbol; *c != '\0'; ++c)
            CHECK((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
        if (p.kind == ParamKind::Toggle)
            CHECK(p.min == 0.0f && p.max == 1.0f);
    }
    CHECK(info(Param::GlobalBypass).kind == ParamKind::Toggle);
    CHECK(info(Param::EchoNote).max == static_cast<float>(kNoteDivisionCount - 1));
    for (const ParamInfo& p : kParams)
        if (p.kind == ParamKind::Choice)
            CHECK(p.min == 0.0f && p.max == static_cast<float>(choiceCount(p.id) - 1));
}

TEST(display_order_is_a_permutation)
{
    std::set<uint32_t> seen;
    for (Param p : kDisplayOrder) {
        CHECK(index(p) < kParamCount);
        CHECK(seen.insert(index(p)).second);
    }
    CHECK(seen.size() == kParamCount);
}

TEST(control_pages_are_valid)
{
    CHECK(!kControlPages.empty());
    std::set<std::string> names;
    std::set<uint32_t> mapped;
    for (const ControlPage& page : kControlPages) {
        CHECK(std::strlen(page.section) > 0);
        CHECK(names.insert(page.name).second);
        bool any = false;
        for (Param p : page.params) {
            CHECK(p == kNoParam || index(p) < kParamCount);
            if (p != kNoParam) {
                any = true;
                mapped.insert(index(p));
            }
        }
        CHECK(any);
    }
    // Every parameter can be reached from a controller.
    CHECK(mapped.size() == kParamCount);
    // The first knobs of the listing are the Main page, for controllers that map by eights.
    for (uint32_t i = 0; i < kControlPageSlots; ++i)
        CHECK(kDisplayOrder[i] == kControlPages[0].params[i]);
}

TEST(design_defaults)
{
    CHECK(info(Param::GlobalBypass).def == 0.0f);
    CHECK(info(Param::HpfEnabled).def == 1.0f);
    CHECK(info(Param::CompressAmount).def == 70.0f);
    CHECK(info(Param::EchoRepeats).max == 95.0f);
    CHECK(info(Param::EchoRepeats).def == 25.0f);
    CHECK(info(Param::InputGainDb).min == -24.0f && info(Param::InputGainDb).max == 24.0f);
    CHECK(info(Param::OutputGainDb).min == -24.0f && info(Param::OutputGainDb).max == 12.0f);
    CHECK(std::strcmp(kNoteDivisions[kNoteDivisionDefault].label, "1/4") == 0);
    CHECK(std::strcmp(choiceLabel(Param::VoiceRegister, static_cast<uint32_t>(info(Param::VoiceRegister).def)),
                      "Neutral") == 0);
}

TEST(note_divisions)
{
    CHECK(kNoteDivisions.size() == 21);
    CHECK_NEAR(noteDivisionSeconds(9, 120.0), 0.5, 1e-12);            // 1/4 at 120 BPM
    CHECK_NEAR(noteDivisionSeconds(6, 120.0), 0.25, 1e-12);           // 1/8
    CHECK_NEAR(noteDivisionSeconds(7, 120.0), 0.25 * 2.0 / 3.0, 1e-12); // 1/8 triplet
    CHECK_NEAR(noteDivisionSeconds(8, 120.0), 0.375, 1e-12);          // 1/8 dotted
    CHECK_NEAR(noteDivisionSeconds(18, 60.0), 8.0, 1e-12);            // 2/1 at 60 BPM
    for (uint32_t i = 0; i < kNoteDivisionCount; i += 3) {
        CHECK_NEAR(kNoteDivisions[i + 1].beats, kNoteDivisions[i].beats * 2.0 / 3.0, 1e-12);
        CHECK_NEAR(kNoteDivisions[i + 2].beats, kNoteDivisions[i].beats * 1.5, 1e-12);
    }
}

TEST(values_are_clamped)
{
    CHECK(Engine::clampToRange(Param::InputGainDb, 100.0f) == 24.0f);
    CHECK(Engine::clampToRange(Param::InputGainDb, -100.0f) == -24.0f);
    CHECK(Engine::clampToRange(Param::HpfEnabled, 0.7f) == 1.0f);
    CHECK(Engine::clampToRange(Param::HpfEnabled, 0.2f) == 0.0f);
    CHECK(Engine::clampToRange(Param::EchoNote, 3.6f) == 4.0f);
    CHECK(Engine::clampToRange(Param::EchoNote, 99.0f) == 20.0f);
    CHECK(Engine::clampToRange(Param::OutputGainDb, std::nanf("")) == 0.0f);

    Engine e;
    e.setParameter(Param::EchoRepeats, 100.0f);
    CHECK(e.parameter(Param::EchoRepeats) == 95.0f);
}
