// SPDX-License-Identifier: GPL-3.0-or-later
#include "UserPresets.hpp"

#include "Settings.hpp"
#include "engine/Engine.hpp"
#include "engine/Presets.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

namespace titv::ui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kNameKey = "name";
constexpr const char* kFormatKey = "format";

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Reads the "name=" line of a preset file.
std::string storedName(const fs::path& file)
{
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("name=", 0) == 0)
            return UserPresets::cleanName(line.substr(5));
    return {};
}

} // namespace

UserPresets::UserPresets(fs::path folder)
    : folder_(std::move(folder))
{
}

fs::path UserPresets::defaultFolder()
{
    const fs::path dir = configDirectory();
    return dir.empty() ? dir : dir / "presets";
}

std::string UserPresets::cleanName(const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
        if (c >= 0x20 && c != 0x7f)
            out.push_back(static_cast<char>(c));
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > kMaxNameLength) {
        out.resize(kMaxNameLength);
        // Do not cut a UTF-8 sequence in half.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80) != 0)
            out.pop_back();
    }
    return out;
}

fs::path UserPresets::fileFor(const std::string& name) const
{
    // Characters that are not allowed in file names on at least one OS become '_'.
    std::string file;
    for (char c : cleanName(name))
        file.push_back(std::strchr("/\\:*?\"<>|", c) != nullptr ? '_' : c);
    if (file == "." || file == "..")
        file = "_";
    return folder_ / pathFromUtf8(file + kExtension);
}

std::vector<std::string> UserPresets::list() const
{
    std::vector<std::string> names;
    std::error_code ec;
    if (folder_.empty() || !fs::is_directory(folder_, ec))
        return names;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder_, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != kExtension)
            continue;
        std::string name = storedName(entry.path());
        if (name.empty())
            name = cleanName(utf8FromPath(entry.path().stem()));
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    return names;
}

fs::path UserPresets::find(const std::string& name) const
{
    std::error_code ec;
    const std::string clean = cleanName(name);
    if (clean.empty())
        return {};
    const fs::path direct = fileFor(clean);
    if (fs::exists(direct, ec) && storedName(direct) == clean)
        return direct;
    if (fs::is_directory(folder_, ec))
        for (const fs::directory_entry& entry : fs::directory_iterator(folder_, ec))
            if (entry.path().extension() == kExtension &&
                (storedName(entry.path()) == clean ||
                 (storedName(entry.path()).empty() && cleanName(utf8FromPath(entry.path().stem())) == clean)))
                return entry.path();
    return fs::exists(direct, ec) ? direct : fs::path();
}

bool UserPresets::exists(const std::string& name) const
{
    return !find(name).empty();
}

std::optional<std::array<float, kParamCount>> UserPresets::load(const std::string& name) const
{
    const fs::path file = find(name);
    if (file.empty())
        return std::nullopt;
    std::ifstream in(file);
    if (!in)
        return std::nullopt;

    std::array<float, kParamCount> values = presetValues(kPresets[0]);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string text = line.substr(eq + 1);
        for (const ParamInfo& p : kParams) {
            if (key != p.symbol || !presetControls(p.id))
                continue;
            // Classic locale: "0.5" whatever the host's language settings.
            std::istringstream parse(text);
            parse.imbue(std::locale::classic());
            float v = 0.0f;
            if (parse >> v)
                values[index(p.id)] = Engine::clampToRange(p.id, v);
            break;
        }
    }
    return values;
}

bool UserPresets::save(const std::string& name, const std::array<float, kParamCount>& values) const
{
    const std::string clean = cleanName(name);
    if (clean.empty() || folder_.empty())
        return false;
    std::error_code ec;
    fs::create_directories(folder_, ec);

    // Replace the existing file of that preset, wherever it is; write next to the
    // target, then rename, so a crash never leaves a half-written preset.
    const fs::path existing = find(clean);
    const fs::path target = existing.empty() ? fileFor(clean) : existing;
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out)
            return false;
        out.imbue(std::locale::classic());
        out.precision(9); // enough digits to read back the same float
        out << "# This Is The Voice preset\n";
        out << kFormatKey << '=' << kFormatVersion << '\n';
        out << kNameKey << '=' << clean << '\n';
        for (const ParamInfo& p : kParams) {
            if (!presetControls(p.id))
                continue;
            out << p.symbol << '=' << values[index(p.id)] << '\n';
        }
        if (!out.flush())
            return false;
    }
    fs::rename(temp, target, ec);
    return !ec;
}

bool UserPresets::remove(const std::string& name) const
{
    std::error_code ec;
    const fs::path file = find(name);
    return !file.empty() && fs::remove(file, ec);
}

} // namespace titv::ui
