// SPDX-License-Identifier: MIT
// HTTPS GET without a library to link: WinHTTP on Windows, the system's curl elsewhere
// (always present on macOS, nearly always on Linux; without it the check just does
// not happen).
#include "UpdateCheck.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace titv::ui {

namespace {
constexpr std::size_t kMaxBody = 256 * 1024; // a release description is a few kilobytes, don't allocate the entire world :p
}

#ifdef _WIN32

void HttpRequest::cancel()
{
    cancelled = true;
    // Closing the handle makes the blocked call in the worker fail at once.
    std::lock_guard<std::mutex> guard(lock);
    if (handle != nullptr) {
        WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        handle = nullptr;
    }
}

namespace {

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

} // namespace

std::string httpGet(const std::string& url, const std::string& userAgent, int timeoutSeconds, HttpRequest& request)
{
    // Only https://host/path addresses are ever passed in.
    const std::string prefix = "https://";
    if (url.rfind(prefix, 0) != 0)
        return {};
    const std::size_t slash = url.find('/', prefix.size());
    const std::wstring host = widen(url.substr(prefix.size(), slash - prefix.size()));
    const std::wstring path = widen(slash == std::string::npos ? "/" : url.substr(slash));

    std::string body;
    HINTERNET session = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return {};
    const int ms = timeoutSeconds * 1000;
    WinHttpSetTimeouts(session, ms, ms, ms, ms);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = connection == nullptr ? nullptr
                                          : WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (req != nullptr) {
        bool registered = false;
        {
            std::lock_guard<std::mutex> guard(request.lock);
            if (!request.cancelled) {
                request.handle = req;
                registered = true;
            }
        }
        if (registered &&
            WinHttpAddRequestHeaders(req, L"Accept: application/vnd.github+json", static_cast<DWORD>(-1L),
                                     WINHTTP_ADDREQ_FLAG_ADD) &&
            WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            char chunk[4096];
            DWORD got = 0;
            while (status == 200 && body.size() < kMaxBody && WinHttpReadData(req, chunk, sizeof(chunk), &got) && got > 0)
                body.append(chunk, got);
            if (status != 200)
                body.clear();
        }
        std::lock_guard<std::mutex> guard(request.lock);
        if (request.handle != nullptr) { // not already closed by cancel()
            WinHttpCloseHandle(req);
            request.handle = nullptr;
        }
    }
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return body;
}

#else

void HttpRequest::cancel()
{
    cancelled = true;
    std::lock_guard<std::mutex> guard(lock);
    if (child > 0)
        kill(static_cast<pid_t>(child), SIGKILL);
}

// Runs one command with its output on a pipe. Everything the child needs is prepared
// before fork, and only async-signal-safe calls run between fork and exec (the host is
// multithreaded). Returns false with `missing` set when the program is not installed.
static bool runCommand(const char* const* argv, HttpRequest& request, std::string& body, bool& missing)
{
    missing = false;
    int fds[2];
    if (pipe(fds) != 0)
        return false;

    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> guard(request.lock);
        if (request.cancelled) {
            close(fds[0]);
            close(fds[1]);
            return false;
        }
        pid = fork();
        if (pid == 0) {
            dup2(fds[1], STDOUT_FILENO);
            const int null = open("/dev/null", O_RDWR);
            if (null >= 0) {
                dup2(null, STDIN_FILENO);
                dup2(null, STDERR_FILENO);
            }
            for (int fd = 3, n = static_cast<int>(sysconf(_SC_OPEN_MAX)); fd < n && fd < 4096; ++fd)
                close(fd);
            execvp(argv[0], const_cast<char* const*>(argv));
            _exit(127);
        }
        if (pid > 0)
            request.child = pid;
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return false;
    }

    body.clear();
    char chunk[4096];
    for (;;) {
        const ssize_t n = read(fds[0], chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        body.append(chunk, static_cast<std::size_t>(n));
        if (body.size() >= kMaxBody)
            break;
    }
    close(fds[0]);

    int status = 0;
    {
        std::lock_guard<std::mutex> guard(request.lock);
        if (body.size() >= kMaxBody)
            kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        request.child = 0;
    }
    missing = WIFEXITED(status) && WEXITSTATUS(status) == 127;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// curl first (always on macOS, nearly always on Linux), then wget. No benefits to include third party libraries, and the system's curl is always present on macOS and nearly always on Linux. The user can install wget if they want to.
std::string httpGet(const std::string& url, const std::string& userAgent, int timeoutSeconds, HttpRequest& request)
{
    const std::string timeout = std::to_string(timeoutSeconds);
    const std::string wgetTimeout = "--timeout=" + timeout;
    const std::string wgetAgent = "--user-agent=" + userAgent;
    const char* curl[] = { "curl", "--silent", "--fail", "--location", "--proto", "=https", "--max-time", timeout.c_str(),
                           "--user-agent", userAgent.c_str(), "--header", "Accept: application/vnd.github+json",
                           url.c_str(), nullptr };
    const char* wget[] = { "wget", "--quiet", "--output-document=-", wgetTimeout.c_str(), wgetAgent.c_str(),
                           "--header=Accept: application/vnd.github+json", url.c_str(), nullptr };

    std::string body;
    for (const char* const* argv : { static_cast<const char* const*>(curl), static_cast<const char* const*>(wget) }) {
        bool missing = false;
        if (runCommand(argv, request, body, missing))
            return body;
        if (!missing)
            break; // the program ran and failed (offline, HTTP error): another would fail too
    }
    return {};
}

#endif

} // namespace titv::ui
