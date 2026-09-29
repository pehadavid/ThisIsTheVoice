// SPDX-License-Identifier: MIT
#include "UpdateCheck.hpp"

#include <cctype>
#include <cstdlib>

namespace titv::ui {

namespace {

constexpr const char* kRepository = "pehadavid/ThisIsTheVoice";
constexpr const char* kDevTag = "dev-build";
constexpr std::size_t kMaxLabelLength = 40;

struct Version {
    int major = 0, minor = 0, patch = 0;
    int devRun = -1; // -dev.<run>, -1 for a full release
};

// x.y.z or x.y.z-dev.<run>, with an optional leading v. Nothing for anything else.
std::optional<Version> parseVersion(const std::string& text)
{
    Version v;
    std::size_t i = text.empty() || (text[0] != 'v' && text[0] != 'V') ? 0 : 1;
    int* parts[3] = { &v.major, &v.minor, &v.patch };
    for (int part = 0; part < 3; ++part) {
        std::size_t digits = 0;
        long n = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0 && digits < 6) {
            n = n * 10 + (text[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0)
            return std::nullopt;
        *parts[part] = static_cast<int>(n);
        if (part < 2) {
            if (i >= text.size() || text[i] != '.')
                return std::nullopt;
            ++i;
        }
    }
    if (i == text.size())
        return v;
    if (text.compare(i, 5, "-dev.") != 0)
        return std::nullopt;
    i += 5;
    std::size_t digits = 0;
    long run = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0 && digits < 9) {
        run = run * 10 + (text[i] - '0');
        ++i;
        ++digits;
    }
    if (digits == 0 || i != text.size())
        return std::nullopt;
    v.devRun = static_cast<int>(run);
    return v;
}

bool newerVersion(const Version& a, const Version& than)
{
    if (a.major != than.major)
        return a.major > than.major;
    if (a.minor != than.minor)
        return a.minor > than.minor;
    return a.patch > than.patch;
}

// The value of the first "key": "..." in the JSON text; empty if absent. The release
// object lists tag_name and name before anything nested (assets, author), and neither
// contains an escaped quote, so this is enough and keeps a JSON parser out of the plugin.
std::string jsonString(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    std::size_t at = json.find(needle);
    if (at == std::string::npos)
        return {};
    at += needle.size();
    while (at < json.size() && (json[at] == ' ' || json[at] == ':'))
        ++at;
    if (at >= json.size() || json[at] != '"')
        return {};
    const std::size_t end = json.find('"', at + 1);
    if (end == std::string::npos || end - at - 1 > kMaxLabelLength)
        return {};
    return json.substr(at + 1, end - at - 1);
}

// The last -dev.<run> version in a text such as "Development build 0.9.3-dev.57".
std::string lastWord(const std::string& text)
{
    const std::size_t space = text.find_last_of(' ');
    return space == std::string::npos ? text : text.substr(space + 1);
}

} // namespace

Channel channelFromName(const std::string& name)
{
    if (name == "stable")
        return Channel::Stable;
    if (name == "dev")
        return Channel::Dev;
    return Channel::Local;
}

std::string releaseApiUrl(Channel channel)
{
    const std::string base = std::string("https://api.github.com/repos/") + kRepository + "/releases/";
    switch (channel) {
    case Channel::Stable: return base + "latest";
    case Channel::Dev: return base + "tags/" + kDevTag;
    case Channel::Local: break;
    }
    return {};
}

std::optional<UpdateInfo> updateFromId(Channel channel, const std::string& runningLabel, const std::string& id)
{
    const std::optional<Version> running = parseVersion(runningLabel);
    const std::optional<Version> latest = parseVersion(id);
    if (channel == Channel::Local || !running || !latest)
        return std::nullopt;

    const std::string releasePage = std::string("https://github.com/") + kRepository + "/releases/tag/";
    if (channel == Channel::Stable) {
        // A full release only (tag v<x.y.z>): never a "-dev" label, never a version at or below ours.
        if (id[0] != 'v' || latest->devRun >= 0 || !newerVersion(*latest, *running))
            return std::nullopt;
        return UpdateInfo { id, id.substr(1), releasePage + id };
    }

    // Dev: one pre-release, replaced at every push; only its run number tells builds apart.
    if (id[0] == 'v' || running->devRun < 0 || latest->devRun <= running->devRun)
        return std::nullopt;
    return UpdateInfo { id, id, releasePage + kDevTag };
}

std::optional<UpdateInfo> newerRelease(Channel channel, const std::string& runningLabel, const std::string& releaseJson)
{
    const std::string id = channel == Channel::Stable ? jsonString(releaseJson, "tag_name")
                                                      : lastWord(jsonString(releaseJson, "name"));
    return updateFromId(channel, runningLabel, id);
}

UpdateChecker::~UpdateChecker()
{
    stop();
}

void UpdateChecker::stop()
{
    if (thread_.joinable()) {
        request_.cancel();
        thread_.join();
    }
}

void UpdateChecker::start(Channel channel, std::string runningLabel)
{
    stop();
    finished_ = false;
    answered_ = false;
    result_.reset();
    const std::string url = releaseApiUrl(channel);
    if (url.empty()) {
        finished_ = true;
        return;
    }
    thread_ = std::thread([this, channel, url, label = std::move(runningLabel)] {
        const std::string body = httpGet(url, "ThisIsTheVoice/" + label, 5, request_);
        if (!body.empty()) {
            answered_ = true;
            result_ = newerRelease(channel, label, body);
        }
        finished_ = true;
    });
}

} // namespace titv::ui
