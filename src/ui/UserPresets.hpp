// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/Parameters.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace titv::ui {

// User presets: one text file per preset in <config>/presets, shared by every
// instance and project. Values are stored by parameter symbol, so a file written by
// an older version simply lacks the newer parameters, which keep their defaults.
// Like factory presets, user presets never hold Input nor the bypass.
//
// File format (UTF-8, one "key=value" per line, '#' starts a comment):
//   format=1
//   name=My Lead
//   compress_amount=82
//   ...
//
// UI thread only.
class UserPresets {
public:
    static constexpr int kFormatVersion = 1;
    static constexpr const char* kExtension = ".titvpreset";
    static constexpr size_t kMaxNameLength = 40;

    // Folder holding the presets; defaults to <config>/presets.
    explicit UserPresets(std::filesystem::path folder = defaultFolder());

    static std::filesystem::path defaultFolder();

    // Names of the presets on disk, sorted case-insensitively.
    std::vector<std::string> list() const;

    bool exists(const std::string& name) const;

    // Full parameter set: defaults, overridden by the file. Out-of-range values are
    // clamped; unknown keys are ignored.
    std::optional<std::array<float, kParamCount>> load(const std::string& name) const;

    // Writes (or replaces) a preset. Returns false if the name is empty or the file
    // cannot be written.
    bool save(const std::string& name, const std::array<float, kParamCount>& values) const;

    bool remove(const std::string& name) const;

    // Name cleaned for display and storage: trimmed, control characters removed,
    // length limited. May return an empty string.
    static std::string cleanName(const std::string& name);

    std::filesystem::path fileFor(const std::string& name) const;

private:
    // File holding a listed preset: the one named after it, or else the file whose
    // stored name matches (a preset file renamed or copied by hand).
    std::filesystem::path find(const std::string& name) const;

    std::filesystem::path folder_;
};

} // namespace titv::ui
