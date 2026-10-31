// AliPlatformLinux.cpp — Linux platform adapter implementation.
//
// Build status: STUB — not yet tested on Linux hardware.
// See AliPlatformLinux.h for documentation.

#include "AliPlatformLinux.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sys/statvfs.h>

namespace ali::platform {

LinuxAdapter::LinuxAdapter(std::string data_dir)
    : data_dir_(std::move(data_dir)) {}

StoragePaths LinuxAdapter::storage_paths() const {
    StoragePaths p;
    p.index_root    = std::filesystem::path(data_dir_) / "ali" / "index";
    p.db_path       = std::filesystem::path(data_dir_) / "ali" / "meta.db";
    p.locality_path = std::filesystem::path(data_dir_) / "ali" / "locality";
    p.log_path      = std::filesystem::path(data_dir_) / "ali" / "ali.log";
    return p;
}

uint64_t LinuxAdapter::free_bytes() const {
    struct statvfs stat{};
    if (::statvfs(data_dir_.c_str(), &stat) != 0) return 0;
    return static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;
}

void LinuxAdapter::log(LogLevel level, const std::string& tag,
                        const std::string& message) const {
    const char* prefix = "INFO";
    switch (level) {
        case LogLevel::Trace: prefix = "TRACE"; break;
        case LogLevel::Debug: prefix = "DEBUG"; break;
        case LogLevel::Info:  prefix = "INFO";  break;
        case LogLevel::Warn:  prefix = "WARN";  break;
        case LogLevel::Error: prefix = "ERROR"; break;
    }
    std::fprintf(stderr, "[ALI/%s] %s: %s\n", prefix, tag.c_str(), message.c_str());
}

int64_t LinuxAdapter::unix_seconds() const {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t LinuxAdapter::monotonic_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace ali::platform
