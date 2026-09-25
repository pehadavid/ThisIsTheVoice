// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

constexpr const char* kLanguageKey = "language=";

} // namespace

Language loadLanguage()
{
    const fs::path file = settingsFile();
    if (file.empty())
        return Language::English;

    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(kLanguageKey, 0) == 0)
            return line.substr(std::char_traits<char>::length(kLanguageKey)) == "fr" ? Language::French
                                                                                  : Language::English;
    return Language::English;
}

void saveLanguage(Language lang)
{
    const fs::path file = settingsFile();
    if (file.empty())
        return;

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    out << kLanguageKey << (lang == Language::French ? "fr" : "en") << '\n';
}

} // namespace titv::ui
