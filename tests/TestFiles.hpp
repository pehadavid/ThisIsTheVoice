// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Portable helpers for tests that touch files and the environment.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

namespace titv::test {

inline void setEnv(const char* name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

// A fresh folder under the system temp directory, removed on destruction.
struct TempFolder {
    std::filesystem::path path;

    explicit TempFolder(const std::string& prefix = "titv-test-")
    {
        std::mt19937_64 rng(std::random_device {}() ^
                            static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        path = std::filesystem::temp_directory_path() / (prefix + std::to_string(rng()));
        std::filesystem::remove_all(path);
    }
    ~TempFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

} // namespace titv::test
