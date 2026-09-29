// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Strings.hpp"

#include <filesystem>
#include <string>

namespace titv::ui {

// Per-user editor preferences, shared by every instance and every project, stored in
// a small text file in the user's configuration directory
// (Linux: $XDG_CONFIG_HOME/ThisIsTheVoice/settings.ini, else ~/.config/...;
// $TITV_CONFIG_DIR replaces the folder on every platform).
// Called from the UI thread only; a missing or unreadable file gives the defaults.
Language loadLanguage();
void saveLanguage(Language lang);

// Update check: on by default, opt-out. The result is remembered so that opening an
// editor does not ask GitHub every time (its API allows few requests per address).
struct UpdateState {
    bool enabled = true;
    long long checkedAt = 0; // Unix time of the last answer from GitHub
    std::string found;       // id of the newer build it reported ("" when up to date)
    std::string skipped;     // id of the build the user chose to ignore
};
UpdateState loadUpdateState();
void saveUpdateState(const UpdateState& state);

// The user's configuration folder for the plugin (empty if it cannot be determined).
std::filesystem::path configDirectory();

// UTF-8 text <-> paths. A plain std::string is read in the local code page on
// Windows, which would garble accented preset names.
std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string utf8FromPath(const std::filesystem::path& path);

} // namespace titv::ui
