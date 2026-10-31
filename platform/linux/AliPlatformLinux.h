// AliPlatformLinux.h — Linux platform adapter for the ALI engine.
//
// Implements PlatformAdapter using POSIX APIs. Intended for desktop Linux
// builds using the Qt/C++ application shell.
//
// Build status: STUB — not yet tested on Linux hardware.
// Do not claim Linux support until this adapter builds, C API tests pass,
// and index compatibility is verified.

#pragma once

#include "ali/platform/platform_adapter.h"
#include <string>

namespace ali::platform {

class LinuxAdapter final : public PlatformAdapter {
public:
    explicit LinuxAdapter(std::string data_dir);

    [[nodiscard]] StoragePaths storage_paths()  const override;
    [[nodiscard]] uint64_t     free_bytes()     const override;
    void log(LogLevel level, const std::string& tag,
             const std::string& message) const override;
    [[nodiscard]] int64_t  unix_seconds()  const override;
    [[nodiscard]] uint64_t monotonic_ms()  const override;

private:
    std::string data_dir_;
};

} // namespace ali::platform
