// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace titv::ui {

// Which releases a build follows. Set at build time (TITV_CHANNEL):
//   Stable  built from main: offered the latest full release (tag v<x.y.z>);
//   Dev     built from dev: offered the "Development build" pre-release (tag dev-build)
//           when its run number (x.y.z-dev.<run>) is above its own;
//   Local   any other build (from source, pull request): never checks.
enum class Channel { Local, Dev, Stable };

Channel channelFromName(const std::string& name);

// A newer build than the running one.
struct UpdateInfo {
    std::string id;    // what "skip this update" remembers: the tag, or the dev label
    std::string label; // version shown to the user, e.g. 0.9.3 or 0.9.3-dev.57
    std::string url;   // release page in the browser
};

// GitHub API address that describes the release a channel follows (empty for Local).
std::string releaseApiUrl(Channel channel);

// The update an id stands for (as kept in the settings), if it is still newer than the
// running build. Same rules as newerRelease.
std::optional<UpdateInfo> updateFromId(Channel channel, const std::string& runningLabel, const std::string& id);

// Reads the release description returned by that address and tells whether it is newer
// than `runningLabel` (the build's version label). Nothing for a malformed answer, an
// older or equal release, or Channel::Local. Pure function: no I/O.
std::optional<UpdateInfo> newerRelease(Channel channel, const std::string& runningLabel, const std::string& releaseJson);

// GET over HTTPS, at most `timeoutSeconds`. Empty on any failure (offline, no curl on
// Linux/macOS, HTTP error). Blocks; `cancel` stops it early from another thread.
struct HttpRequest {
    std::mutex lock;
    long long child = 0; // process to stop on cancel (POSIX)
    std::atomic<bool> cancelled { false };
    void* handle = nullptr; // WinHTTP request to close on cancel (Windows)
    void cancel();
};
std::string httpGet(const std::string& url, const std::string& userAgent, int timeoutSeconds, HttpRequest& request);

// Runs the check on a worker thread, so the editor never waits for the network.
// Destroying it cancels a request in progress.
class UpdateChecker {
public:
    UpdateChecker() = default;
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    void start(Channel channel, std::string runningLabel);
    // The result once the worker has finished; false while it runs or before start().
    bool finished() const { return finished_.load(); }
    // What the check found (nothing when up to date or on failure). Valid once finished().
    const std::optional<UpdateInfo>& result() const { return result_; }
    // True if the request reached GitHub and got an answer, so a "checked" time is worth keeping.
    bool answered() const { return answered_.load(); }

private:
    void stop();

    std::thread thread_;
    HttpRequest request_;
    std::atomic<bool> finished_ { false };
    std::atomic<bool> answered_ { false };
    std::optional<UpdateInfo> result_;
};

} // namespace titv::ui
