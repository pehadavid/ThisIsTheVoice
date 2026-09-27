// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenUrl.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace titv::ui {

#ifdef _WIN32

// Under Wine this goes through winebrowser, which hands the address to the Linux
// desktop's browser.
bool openUrl(const char* url)
{
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

#else

// The opener runs in a grandchild, so the host never has a child process to reap, and
// it does not inherit the host's files (audio devices, sockets). Only async-signal-safe
// calls between fork and exec: the host is multithreaded.
bool openUrl(const char* url)
{
#ifdef __APPLE__
    const char* opener = "/usr/bin/open";
#else
    const char* opener = "xdg-open";
#endif
    const pid_t child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        if (fork() == 0) {
            setsid();
#ifdef __linux__
            close_range(3, ~0U, 0);
#else
            for (int fd = 3, n = static_cast<int>(sysconf(_SC_OPEN_MAX)); fd < n; ++fd)
                close(fd);
#endif
            execlp(opener, opener, url, static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return true;
}

#endif

} // namespace titv::ui
