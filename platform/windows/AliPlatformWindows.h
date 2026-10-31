// AliPlatformWindows.h — Windows platform adapter for the ALI engine.
//
// Implements PlatformAdapter using Win32 APIs. Intended for Windows builds
// using the Qt/C++ or C#/.NET application shell.
//
// Build status: STUB — not yet tested on Windows hardware.
// Do not claim Windows support until this adapter builds, C API tests pass,
// and index compatibility is verified.

#pragma once

#include "ali/platform/platform_adapter.h"
#include <string>

namespace ali::platform {

class WindowsAdapter final : public PlatformAdapter {
public:
    explicit WindowsAdapter(std::string app_data_dir);

    [[nodiscard]] StoragePaths storage_paths()  const override;
    [[nodiscard]] uint64_t     free_bytes()     const override;
    void log(LogLevel level, const std::string& tag,
             const std::string& message) const override;
    [[nodiscard]] int64_t  unix_seconds()  const override;
    [[nodiscard]] uint64_t monotonic_ms()  const override;

private:
    std::string app_data_dir_;
};

} // namespace ali::platform
