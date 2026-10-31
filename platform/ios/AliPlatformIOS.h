// AliPlatformIOS.h — iOS platform adapter for the ALI engine.
//
// Implements PlatformAdapter for iOS using POSIX filesystem APIs.
// Background execution on iOS is highly restricted — the crawler must be
// designed as opportunistic work that may be suspended at any time.
//
// Build status: STUB — not yet tested on iOS hardware.
// Do not claim iOS support until this adapter builds, C API tests pass,
// and lifecycle tests pass on a real device.
//
// iOS-specific considerations:
//   - Use Background App Refresh for crawl scheduling where available.
//   - All persistent data goes in the app's Application Support directory.
//   - The engine must tolerate being killed mid-write (crash-safe publication).

#pragma once

#include "ali/platform/platform_adapter.h"
#include <string>

namespace ali::platform {

class IOSAdapter final : public PlatformAdapter {
public:
    /// app_support_dir: path to the app's Application Support directory.
    explicit IOSAdapter(std::string app_support_dir);

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
