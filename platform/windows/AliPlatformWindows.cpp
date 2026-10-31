// AliPlatformWindows.cpp — Windows platform adapter implementation.
//
// Uses Win32 GetDiskFreeSpaceExA for storage queries and QueryPerformanceCounter
// for monotonic time. OutputDebugStringA for debug logging.
//
// Build status: STUB — not yet tested on Windows hardware.
// Guarded by _WIN32 so it compiles only on Windows toolchains.

#include "AliPlatformWindows.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace ali::platform {

WindowsAdapter::WindowsAdapter(std::string app_data_dir)
    : app_data_dir_(std::move(app_data_dir)) {}

StoragePaths WindowsAdapter::storage_paths() const {
    StoragePaths p;
    p.index_root    = std::filesystem::path(app_data_dir_) / "ALI" / "index";
    p.db_path       = std::filesystem::path(app_data_dir_) / "ALI" / "meta.db";
    p.locality_path = std::filesystem::path(app_data_dir_) / "ALI" / "locality";
    p.log_path      = std::filesystem::path(app_data_dir_) / "ALI" / "ali.log";
    return p;
}

uint64_t WindowsAdapter::free_bytes() const {
#ifdef _WIN32
    ULARGE_INTEGER free_bytes{};
    if (GetDiskFreeSpaceExA(app_data_dir_.c_str(), &free_bytes, nullptr, nullptr)) {
        return static_cast<uint64_t>(free_bytes.QuadPart);
    }
#endif
    return 0;
}

void WindowsAdapter::log(LogLevel level, const std::string& tag,
                          const std::string& message) const {
    const char* prefix = "INFO";
    switch (level) {
        case LogLevel::Trace: prefix = "TRACE"; break;
        case LogLevel::Debug: prefix = "DEBUG"; break;
        case LogLevel::Info:  prefix = "INFO";  break;
        case LogLevel::Warn:  prefix = "WARN";  break;
        case LogLevel::Error: prefix = "ERROR"; break;
    }
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "[ALI/%s] %s: %s\n",
                  prefix, tag.c_str(), message.c_str());
#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    std::fputs(buf, stderr);
#endif
}

int64_t WindowsAdapter::unix_seconds() const {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t WindowsAdapter::monotonic_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace ali::platform
