// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestFiles.hpp"
#include "TestHarness.hpp"

#include "engine/Engine.hpp"
#include "engine/Presets.hpp"
#include "ui/UserPresets.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace titv;
using namespace titv::ui;
namespace fs = std::filesystem;

using titv::test::TempFolder;

TEST(user_preset_round_trip)
{
    TempFolder dir;
    UserPresets store(dir.path);
    CHECK(store.list().empty());

    auto values = presetValues(kPresets[2]); // Dense Lead
    values[index(Param::ToneAirDb)] = 3.25f;
    values[index(Param::EchoNote)] = 14.0f;
    values[index(Param::InputGainDb)] = 9.0f; // not part of presets
    CHECK(store.save("  My Lead  ", values));
    CHECK(store.exists("My Lead"));

    const auto names = store.list();
    CHECK(names.size() == 1 && names[0] == "My Lead");

    const auto loaded = store.load("My Lead");
    CHECK(loaded.has_value());
    for (const ParamInfo& p : kParams) {
        if (presetControls(p.id))
            CHECK((*loaded)[index(p.id)] == values[index(p.id)]);
        else
            CHECK((*loaded)[index(p.id)] == p.def); // Input and bypass never stored
    }

    // Replacing keeps a single entry.
    values[index(Param::CompressAmount)] = 10.0f;
    CHECK(store.save("My Lead", values));
    CHECK(store.list().size() == 1);
    CHECK((*store.load("My Lead"))[index(Param::CompressAmount)] == 10.0f);

    CHECK(store.remove("My Lead"));
    CHECK(!store.exists("My Lead") && store.list().empty());
    CHECK(!store.load("My Lead").has_value());
}

TEST(user_preset_files_are_tolerant)
{
    TempFolder dir;
    fs::create_directories(dir.path);
    {
        // Written by hand or by another version: unknown key, missing parameters,
        // an out-of-range value, CRLF line ends, a comment.
        std::ofstream out(dir.path / "old.titvpreset");
        out << "# comment\r\nformat=1\r\nname=Old One\r\ncompress_amount=250\r\nfuture_param=3\r\n"
               "tone_air_db=-2.5\r\ninput_gain_db=12\r\n";
    }
    UserPresets store(dir.path);
    const auto names = store.list();
    CHECK(names.size() == 1 && names[0] == "Old One");
    // The file name does not match the stored name: the listed name still finds it.
    const auto loaded = store.load("Old One");
    CHECK(loaded.has_value());
    const auto& v = *loaded;
    CHECK(v[index(Param::CompressAmount)] == 100.0f);           // clamped
    CHECK(v[index(Param::ToneAirDb)] == -2.5f);
    CHECK(v[index(Param::InputGainDb)] == info(Param::InputGainDb).def); // ignored
    CHECK(v[index(Param::EchoRepeats)] == info(Param::EchoRepeats).def); // missing: default

    // Saving it again replaces that file rather than adding a second one.
    CHECK(store.save("Old One", v));
    CHECK(store.list().size() == 1);
    CHECK(store.remove("Old One") && store.list().empty());
}

TEST(user_preset_names_are_safe)
{
    CHECK(UserPresets::cleanName("  \tVoix\x01 douce ") == "Voix douce");
    CHECK(UserPresets::cleanName("   ").empty());
    CHECK(UserPresets::cleanName(std::string(100, 'a')).size() == UserPresets::kMaxNameLength);
    // Never cut a multi-byte character: 39 'a' then "é" (2 bytes) exceeds 40.
    const std::string cut = UserPresets::cleanName(std::string(39, 'a') + "\xC3\xA9");
    CHECK(cut == std::string(39, 'a'));

    TempFolder dir;
    UserPresets store(dir.path);
    // Path separators and reserved characters cannot escape the folder.
    CHECK(store.fileFor("../x").parent_path() == dir.path);
    CHECK(store.fileFor("a/b").filename() == "a_b.titvpreset");
    CHECK(store.fileFor("..").filename() == "_.titvpreset");
    CHECK(!store.save("   ", presetValues(kPresets[0])));
    CHECK(store.save("Écho à l'ancienne", presetValues(kPresets[5])));
    CHECK(store.list().front() == "Écho à l'ancienne");
}

#include "ui/PresetBrowser.hpp"

TEST(preset_browser_state_and_navigation)
{
    TempFolder dir;
    PresetBrowser browser { UserPresets(dir.path) };
    CHECK(browser.entries().size() == kPresets.size());
    CHECK(browser.stateValue() == "init");

    // Factory choice round-trips through the state.
    CHECK(browser.restore("dense_lead"));
    CHECK(browser.stateValue() == "dense_lead" && browser.currentIndex() == 2);
    CHECK(!browser.isModified(presetValues(kPresets[2])));
    auto tweaked = presetValues(kPresets[2]);
    tweaked[index(Param::ToneAirDb)] += 1.0f;
    CHECK(browser.isModified(tweaked));
    tweaked = presetValues(kPresets[2]);
    tweaked[index(Param::InputGainDb)] = 7.0f; // Input is not part of presets
    CHECK(!browser.isModified(tweaked));
    CHECK(!browser.restore("no_such_preset") && browser.stateValue() == "init");

    // Saving makes the user preset current, listed after the factory ones.
    auto mine = presetValues(kPresets[4]);
    mine[index(Param::ColorChorus)] = 33.0f;
    CHECK(browser.saveUser("Zed", mine));
    CHECK(browser.saveUser("alpha", mine));
    CHECK(browser.entries().size() == kPresets.size() + 2);
    CHECK(browser.entries()[kPresets.size()].name == "alpha"); // sorted
    CHECK(browser.stateValue() == "user:alpha");
    CHECK(!browser.isModified(mine));

    // Navigation wraps around factory and user presets.
    CHECK(browser.neighbour(1).name == "Zed");
    CHECK(!browser.neighbour(-1).user && browser.neighbour(-1).factory == kPresets.size() - 1);
    browser.select(browser.entries()[browser.entries().size() - 1], mine);
    CHECK(!browser.neighbour(1).user && browser.neighbour(1).factory == 0);

    // Restoring a user preset from the state loads its values for comparison.
    PresetBrowser other { UserPresets(dir.path) };
    CHECK(other.restore("user:Zed") && other.current().name == "Zed");
    CHECK(!other.isModified(mine));
    const auto values = other.valuesOf(other.current());
    CHECK(values.has_value() && (*values)[index(Param::ColorChorus)] == 33.0f);

    // A project whose user preset does not exist here still opens, with its name.
    PresetBrowser elsewhere { UserPresets(dir.path / "empty") };
    CHECK(elsewhere.restore("user:Zed") && elsewhere.stateValue() == "user:Zed");
    CHECK(elsewhere.currentIndex() == -1);
    CHECK(!elsewhere.valuesOf(elsewhere.current()).has_value());
    CHECK(elsewhere.neighbour(1).factory == 0 || elsewhere.neighbour(1).user);

    CHECK(browser.removeUser("Zed"));
    CHECK(browser.entries().size() == kPresets.size() + 1);
}

#include <clocale>
#include <locale>

TEST(user_preset_files_ignore_the_system_locale)
{
    // A host running in French must still write "0.5", not "0,5".
    std::locale previous;
    try {
        std::locale::global(std::locale("fr_FR.UTF-8"));
    } catch (const std::exception&) {
        std::printf("    (fr_FR.UTF-8 not installed: checked with the default locale only)\n");
    }
    std::setlocale(LC_ALL, "fr_FR.UTF-8");

    TempFolder dir;
    UserPresets store(dir.path);
    auto values = presetValues(kPresets[0]);
    values[index(Param::ToneAirDb)] = 2.5f;
    CHECK(store.save("Locale", values));
    std::ifstream in(store.fileFor("Locale"));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("tone_air_db=2.5\n") != std::string::npos);
    CHECK((*store.load("Locale"))[index(Param::ToneAirDb)] == 2.5f);

    std::locale::global(previous);
    std::setlocale(LC_ALL, "C");
}
