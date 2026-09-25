// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Strings.hpp"

#include <filesystem>

namespace titv::ui {

// Per-user editor preferences, shared by every instance and every project, stored in
// a small text file in the user's configuration directory
// (Linux: $XDG_CONFIG_HOME/ThisIsTheVoice/settings.ini, else ~/.config/...;
// $TITV_CONFIG_DIR replaces the folder on every platform).
// Called from the UI thread only; a missing or unreadable file gives the defaults.
Language loadLanguage();
void saveLanguage(Language lang);

// The user's configuration folder for the plugin (empty if it cannot be determined).
std::filesystem::path configDirectory();

// UTF-8 text <-> paths. A plain std::string is read in the local code page on
// Windows, which would garble accented preset names.
std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string utf8FromPath(const std::filesystem::path& path);

} // namespace titv::ui
