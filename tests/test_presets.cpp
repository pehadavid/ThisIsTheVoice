// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineHelpers.hpp"
#include "TestHarness.hpp"

#include "engine/Presets.hpp"

#include <cstring>
#include <iterator>
#include <set>
#include <string>

using namespace titv;
using namespace titv::test;

// Saved in projects: existing ids must never change (append new presets only).
TEST(preset_ids_are_frozen)
{
    static const char* const kFrozen[] = { "init", "natural_voice", "dense_lead", "wide_backing",
                                           "spoken_word", "ad_lib", "ambience" };
    CHECK(kPresets.size() >= std::size(kFrozen));
    for (size_t i = 0; i < std::size(kFrozen) && i < kPresets.size(); ++i)
        CHECK(std::strcmp(kPresets[i].id, kFrozen[i]) == 0);
    for (size_t i = 0; i < kPresets.size(); ++i)
        CHECK(findPreset(kPresets[i].id) == static_cast<int>(i));
    CHECK(findPreset("unknown") == -1);
    CHECK(findPreset("init_") == -1);
}

TEST(presets_are_valid)
{
    std::set<std::string> ids;
    for (const Preset& p : kPresets) {
        CHECK(ids.insert(p.id).second);
        CHECK(std::strlen(p.nameEnglish) > 0 && std::strlen(p.nameFrench) > 0);
        std::set<Param> seen;
        for (const auto& [param, value] : p.values) {
            CHECK(seen.insert(param).second);           // each parameter once
            CHECK(presetControls(param));               // never Input or Bypass
            CHECK(Engine::clampToRange(param, value) == value); // in range, valid step
        }
    }
    // Init is the neutral registry state.
    const auto init = presetValues(kPresets[0]);
    for (const ParamInfo& p : kParams)
        CHECK(init[index(p.id)] == p.def);
}

TEST(presets_render_cleanly_at_a_similar_level)
{
    const Buffer voice = voiceLike(48000.0, 6.0, -18.0f);
    auto levelOf = [&](const Preset& preset, Stereo& out) {
        Engine e;
        e.prepare(48000.0, 512);
        const auto v = presetValues(preset);
        for (uint32_t p = 0; p < kParamCount; ++p)
            e.setParameter(static_cast<Param>(p), v[p]);
        e.reset();
        out = render(e, voice, nullptr, fixedBlocks(256));
        return 0.5f * (rmsDb(out.l, 96000) + rmsDb(out.r, 96000));
    };
    Stereo out;
    const float init = levelOf(kPresets[0], out);
    for (const Preset& p : kPresets) {
        const float level = levelOf(p, out);
        CHECK(allFinite(out.l) && allFinite(out.r));
        CHECK(peak(out.l) < dsp::dbToGain(1.0f) && peak(out.r) < dsp::dbToGain(1.0f));
        CHECK(std::fabs(level - init) < 1.0f);
    }
}
