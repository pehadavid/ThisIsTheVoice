// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestFiles.hpp"
#include "TestHarness.hpp"

#include "ui/Settings.hpp"
#include "ui/Strings.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

using namespace titv;
using namespace titv::ui;

TEST(help_texts_exist_in_both_languages)
{
    for (const ParamInfo& p : kParams) {
        const char* en = helpText(p.id, Language::English);
        const char* fr = helpText(p.id, Language::French);
        CHECK(std::strlen(en) > 0 && std::strlen(fr) > 0);
        CHECK(std::strcmp(en, fr) != 0);
        // Each help sentence starts with the control's label, in both languages.
        CHECK(std::strncmp(en, p.shortName, std::strlen(p.shortName)) == 0);
        CHECK(std::strncmp(fr, p.shortName, std::strlen(p.shortName)) == 0);
    }
    for (size_t t = 0; t < static_cast<size_t>(Text::Count); ++t) {
        CHECK(std::strlen(kTextsEnglish[t]) > 0);
        CHECK(std::strlen(kTextsFrench[t]) > 0);
    }
}

TEST(language_setting_round_trip)
{
    namespace fs = std::filesystem;
    const titv::test::TempFolder temp("titv-settings-");
    const fs::path dir = temp.path;
    titv::test::setEnv("TITV_CONFIG_DIR", (dir / "ThisIsTheVoice").string());

    CHECK(loadLanguage() == Language::English); // default without a settings file
    saveLanguage(Language::French);
    CHECK(fs::exists(dir / "ThisIsTheVoice" / "settings.ini"));
    CHECK(loadLanguage() == Language::French);
    saveLanguage(Language::English);
    CHECK(loadLanguage() == Language::English);
}
