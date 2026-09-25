// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetBrowser.hpp"

#include <cmath>

namespace titv::ui {

PresetBrowser::PresetBrowser(UserPresets store)
    : store_(std::move(store))
    , reference_(presetValues(kPresets[0]))
{
    refresh();
}

void PresetBrowser::refresh()
{
    entries_.clear();
    for (size_t i = 0; i < kPresets.size(); ++i)
        entries_.push_back({ false, i, {} });
    for (const std::string& name : store_.list())
        entries_.push_back({ true, 0, name });
}

int PresetBrowser::currentIndex() const
{
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (e.user == current_.user && (e.user ? e.name == current_.name : e.factory == current_.factory))
            return static_cast<int>(i);
    }
    return -1;
}

std::string PresetBrowser::displayName(const Entry& e, Language lang) const
{
    if (e.user)
        return e.name;
    const Preset& p = kPresets[e.factory];
    return lang == Language::French ? p.nameFrench : p.nameEnglish;
}

std::string PresetBrowser::stateValue() const
{
    return current_.user ? kUserPrefix + current_.name : kPresets[current_.factory].id;
}

bool PresetBrowser::restore(const std::string& value)
{
    if (value.rfind(kUserPrefix, 0) == 0) {
        current_ = { true, 0, UserPresets::cleanName(value.substr(std::char_traits<char>::length(kUserPrefix))) };
        // Missing here: the project keeps its values, nothing to compare against.
        const auto values = store_.load(current_.name);
        reference_ = values ? *values : reference_;
        return true;
    }
    const int found = findPreset(value.c_str());
    current_ = { false, found >= 0 ? static_cast<size_t>(found) : 0, {} };
    reference_ = presetValues(kPresets[current_.factory]);
    return found >= 0;
}

std::optional<PresetBrowser::Values> PresetBrowser::valuesOf(const Entry& e) const
{
    if (e.user)
        return store_.load(e.name);
    return presetValues(kPresets[e.factory]);
}

void PresetBrowser::select(const Entry& e, const Values& applied)
{
    current_ = e;
    reference_ = applied;
}

const PresetBrowser::Entry& PresetBrowser::neighbour(int step) const
{
    const int count = static_cast<int>(entries_.size());
    const int from = currentIndex();
    // Not listed (a missing user preset): start from the end of the factory list.
    const int base = from >= 0 ? from : (step > 0 ? static_cast<int>(factoryCount()) - 1 : 0);
    return entries_[static_cast<size_t>(((base + step) % count + count) % count)];
}

bool PresetBrowser::isModified(const Values& values) const
{
    for (uint32_t p = 0; p < kParamCount; ++p)
        if (presetControls(static_cast<Param>(p)) && std::fabs(values[p] - reference_[p]) > 1e-3f)
            return true;
    return false;
}

bool PresetBrowser::saveUser(const std::string& name, const Values& values)
{
    const std::string clean = UserPresets::cleanName(name);
    if (!store_.save(clean, values))
        return false;
    refresh();
    // What was saved is what the file holds (Input and bypass left out).
    const auto stored = store_.load(clean);
    select({ true, 0, clean }, stored ? *stored : values);
    return true;
}

bool PresetBrowser::removeUser(const std::string& name)
{
    const bool removed = store_.remove(name);
    refresh();
    return removed;
}

} // namespace titv::ui
