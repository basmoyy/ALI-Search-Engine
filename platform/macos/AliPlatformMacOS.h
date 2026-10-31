// AliPlatformMacOS.h — macOS platform adapter for the ALI engine.
//
// Implements PlatformAdapter for macOS using POSIX filesystem APIs,
// Grand Central Dispatch for background scheduling, and NSFileManager
// for storage path resolution.
//
// Build status: STUB — not yet tested on macOS hardware.
// Do not claim macOS support until this adapter builds, the C API tests
// pass, and index compatibility is verified on this platform.

#pragma once

#include "ali/platform/platform_adapter.h"

#include <string>

namespace ali::platform {

/// macOS platform adapter.
/// Compiled only when targeting macOS (CMake platform guard: APPLE AND NOT IOS).
class MacOSAdapter final : public PlatformAdapter {
public:
    explicit MacOSAdapter(std::string app_support_dir);

    [[nodiscard]] StoragePaths storage_paths()  const override;
    [[nodiscard]] uint64_t     free_bytes()     const override;
    void log(LogLevel level, const std::string& tag,
             const std::string& message) const override;
    [[nodiscard]] int64_t  unix_seconds()  const override;
    [[nodiscard]] uint64_t monotonic_ms()  const override;

private:
    std::string app_support_dir_;
};

} // namespace ali::platform
