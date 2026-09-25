// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Strings.hpp"
#include "UserPresets.hpp"
#include "engine/Presets.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace titv::ui {

// Model behind the editor's preset selector: factory presets followed by the user's
// presets, the current choice, and its encoding in the project state ("<factory id>"
// or "user:<name>"). A project whose user preset is missing on this computer still
// opens with its own values; only the name is shown. UI thread only.
class PresetBrowser {
public:
    using Values = std::array<float, kParamCount>;

    struct Entry {
        bool user = false;
        size_t factory = 0; // index in kPresets when !user
        std::string name;   // user preset name when user
    };

    static constexpr const char* kUserPrefix = "user:";

    explicit PresetBrowser(UserPresets store = UserPresets());

    // Rescans the user presets (another instance may have saved one).
    void refresh();

    const std::vector<Entry>& entries() const { return entries_; }
    size_t factoryCount() const { return kPresets.size(); }

    const Entry& current() const { return current_; }
    // Index of the current preset in entries(), or -1 if it is not listed.
    int currentIndex() const;

    std::string displayName(const Entry& e, Language lang) const;
    std::string stateValue() const;

    // From the project state; returns false (and selects Init) for an unknown factory id.
    bool restore(const std::string& stateValue);

    // Values of an entry, for applying it; nullopt if a user file cannot be read.
    std::optional<Values> valuesOf(const Entry& e) const;

    // Makes an entry current, with the values it applied.
    void select(const Entry& e, const Values& applied);

    // Entry step places away from the current one, wrapping around.
    const Entry& neighbour(int step) const;

    // True when current parameter values differ from the current preset's.
    bool isModified(const Values& values) const;

    bool userExists(const std::string& name) const { return store_.exists(name); }

    // Saves the values as a user preset and makes it current.
    bool saveUser(const std::string& name, const Values& values);
    bool removeUser(const std::string& name);

private:
    UserPresets store_;
    std::vector<Entry> entries_;
    Entry current_;
    Values reference_;
};

} // namespace titv::ui
