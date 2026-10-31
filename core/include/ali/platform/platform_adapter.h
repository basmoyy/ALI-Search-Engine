#pragma once
// ali/platform/platform_adapter.h — Per-OS service abstractions.
//
// The portable core only calls through these interfaces. Platform-specific
// implementations live in platform/<os>/ and are compiled separately.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ali {

/// File-system paths for the engine's data.
struct StoragePaths {
    std::filesystem::path index_root;   ///< segment directories live here
    std::filesystem::path db_path;      ///< metadata SQLite database
    std::filesystem::path locality_path; ///< persisted locality statistics
    std::filesystem::path log_path;
};

/// Logging severity levels.
enum class LogLevel : uint8_t { Trace, Debug, Info, Warn, Error };

/// Abstract interface provided by each platform adapter.
class PlatformAdapter {
public:
    virtual ~PlatformAdapter() = default;

    // ── Storage ──────────────────────────────────────────────────────────────
    [[nodiscard]] virtual StoragePaths  storage_paths()  const = 0;
    [[nodiscard]] virtual uint64_t      free_bytes()     const = 0;

    // ── Logging ──────────────────────────────────────────────────────────────
    virtual void log(LogLevel level, const std::string& tag,
                     const std::string& message) const = 0;

    // ── Time ─────────────────────────────────────────────────────────────────
    [[nodiscard]] virtual int64_t  unix_seconds()   const = 0;
    [[nodiscard]] virtual uint64_t monotonic_ms()   const = 0;
};

} // namespace ali
