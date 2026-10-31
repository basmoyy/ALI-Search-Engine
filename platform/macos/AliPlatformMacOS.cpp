// AliPlatformMacOS.cpp — macOS platform adapter implementation.
//
// Build status: STUB — not yet tested on macOS hardware.
// See platform/macos/AliPlatformMacOS.h for full documentation.

#include "AliPlatformMacOS.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sys/statvfs.h>

namespace ali::platform {

MacOSAdapter::MacOSAdapter(std::string app_support_dir)
    : app_support_dir_(std::move(app_support_dir)) {}

StoragePaths MacOSAdapter::storage_paths() const {
    StoragePaths p;
    p.index_root    = std::filesystem::path(app_support_dir_) / "ALI" / "index";
    p.db_path       = std::filesystem::path(app_support_dir_) / "ALI" / "meta.db";
    p.locality_path = std::filesystem::path(app_support_dir_) / "ALI" / "locality";
    p.log_path      = std::filesystem::path(app_support_dir_) / "ALI" / "ali.log";
    return p;
}

uint64_t MacOSAdapter::free_bytes() const {
    struct statvfs stat{};
    if (::statvfs(app_support_dir_.c_str(), &stat) != 0) return 0;
    return static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;
}

void MacOSAdapter::log(LogLevel level, const std::string& tag,
                        const std::string& message) const {
    const char* prefix = "INFO";
    switch (level) {
        case LogLevel::Trace: prefix = "TRACE"; break;
        case LogLevel::Debug: prefix = "DEBUG"; break;
        case LogLevel::Info:  prefix = "INFO";  break;
        case LogLevel::Warn:  prefix = "WARN";  break;
        case LogLevel::Error: prefix = "ERROR"; break;
    }
    // Route to stderr on macOS; a real implementation would use os_log.
    std::fprintf(stderr, "[ALI/%s] %s: %s\n", prefix, tag.c_str(), message.c_str());
}

int64_t MacOSAdapter::unix_seconds() const {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t MacOSAdapter::monotonic_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace ali::platform
