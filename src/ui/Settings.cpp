// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>

namespace titv::ui {

namespace fs = std::filesystem;

fs::path configDirectory()
{
    // Explicit override on every platform (tests, portable setups).
    if (const char* dir = std::getenv("TITV_CONFIG_DIR"); dir != nullptr && *dir != '\0')
        return pathFromUtf8(dir);

    fs::path base;
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"))
        base = appData;
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        base = fs::path(home) / "Library" / "Application Support";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = fs::path(home) / ".config";
#endif
    return base.empty() ? fs::path() : base / "ThisIsTheVoice";
}

fs::path pathFromUtf8(const std::string& utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8FromPath(const fs::path& path)
{
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

namespace {

fs::path settingsFile()
{
    const fs::path dir = configDirectory();
    return dir.empty() ? dir : dir / "settings.ini";
}

// The file is a list of key=value lines; unknown keys are kept when it is rewritten.
using Entries = std::map<std::string, std::string>;

Entries readEntries()
{
    Entries entries;
    const fs::path file = settingsFile();
    if (file.empty())
        return entries;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0)
            entries[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return entries;
}

void writeEntries(const Entries& entries)
{
    const fs::path file = settingsFile();
    if (file.empty())
        return;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    for (const auto& [key, value] : entries)
        out << key << '=' << value << '\n';
}

std::string valueOf(const Entries& entries, const char* key)
{
    const auto it = entries.find(key);
    return it == entries.end() ? std::string() : it->second;
}

constexpr const char* kLanguageKey = "language";
constexpr const char* kUpdateCheckKey = "update_check";
constexpr const char* kUpdateCheckedKey = "update_checked_at";
constexpr const char* kUpdateFoundKey = "update_found";
constexpr const char* kUpdateSkippedKey = "update_skipped";

} // namespace

Language loadLanguage()
{
    return valueOf(readEntries(), kLanguageKey) == "fr" ? Language::French : Language::English;
}

void saveLanguage(Language lang)
{
    Entries entries = readEntries();
    entries[kLanguageKey] = lang == Language::French ? "fr" : "en";
    writeEntries(entries);
}

UpdateState loadUpdateState()
{
    const Entries entries = readEntries();
    UpdateState state;
    state.enabled = valueOf(entries, kUpdateCheckKey) != "off";
    state.checkedAt = std::atoll(valueOf(entries, kUpdateCheckedKey).c_str());
    state.found = valueOf(entries, kUpdateFoundKey);
    state.skipped = valueOf(entries, kUpdateSkippedKey);
    return state;
}

void saveUpdateState(const UpdateState& state)
{
    Entries entries = readEntries();
    entries[kUpdateCheckKey] = state.enabled ? "on" : "off";
    entries[kUpdateCheckedKey] = std::to_string(state.checkedAt);
    entries[kUpdateFoundKey] = state.found;
    entries[kUpdateSkippedKey] = state.skipped;
    writeEntries(entries);
}

} // namespace titv::ui
