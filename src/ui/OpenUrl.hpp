// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace titv::ui {

// Opens a web address in the user's browser, without waiting for it. Returns false
// if the browser could not be started. Called from the UI thread.
bool openUrl(const char* url);

} // namespace titv::ui
