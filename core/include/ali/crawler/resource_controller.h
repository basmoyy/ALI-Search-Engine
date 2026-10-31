#pragma once
// ali/crawler/resource_controller.h — Device resource arbiter.
//
// The resource controller decides whether background work (crawling,
// compaction, parsing) may proceed given current device conditions.
// Platform adapters implement the abstract interface for each OS.

#include <cstdint>

namespace ali {

/// Device-condition snapshot. Populated by the platform adapter.
struct DeviceConditions {
    bool     network_available     = false;
    bool     network_metered       = true;
    bool     charging              = false;
    uint8_t  battery_level_pct     = 100;   ///< 0–100
    bool     thermal_throttling    = false;
    bool     memory_pressure       = false;  ///< OS low-memory signal
    bool     interactive_search    = false;  ///< a query is in progress
    uint64_t free_storage_bytes    = 0;
};

/// Permitted workload level.
enum class WorkloadPermission : uint8_t {
    Forbidden = 0,   ///< stop all background work
    Minimal   = 1,   ///< single slow background task
    Normal    = 2,   ///< standard background budget
    Elevated  = 3,   ///< charging + unmetered Wi-Fi
};

/// Abstract resource-condition interface. Subclassed per platform.
class ResourceController {
public:
    virtual ~ResourceController() = default;

    /// Platform adapter must implement this to return fresh conditions.
    [[nodiscard]] virtual DeviceConditions conditions() const = 0;

    /// Convenience: map current conditions to a workload permission level.
    [[nodiscard]] WorkloadPermission permitted_workload() const;

    /// Convenience: true only when crawling is allowed at any level.
    [[nodiscard]] bool may_crawl()    const;
    [[nodiscard]] bool may_compact()  const;

protected:
    /// Shared decision logic so all platforms behave identically.
    static WorkloadPermission evaluate(const DeviceConditions& c) noexcept;
};

} // namespace ali
