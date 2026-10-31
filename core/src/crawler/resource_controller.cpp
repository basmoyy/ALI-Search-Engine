// resource_controller.cpp — Shared workload-permission logic for all platforms.

#include "ali/crawler/resource_controller.h"

namespace ali {

WorkloadPermission ResourceController::evaluate(const DeviceConditions& c) noexcept {
    // Interactive search always wins.
    if (c.interactive_search)  return WorkloadPermission::Forbidden;
    if (c.memory_pressure)     return WorkloadPermission::Forbidden;
    if (c.thermal_throttling)  return WorkloadPermission::Forbidden;
    if (!c.network_available)  return WorkloadPermission::Forbidden;

    if (c.battery_level_pct < 15 && !c.charging) return WorkloadPermission::Forbidden;

    if (c.network_metered && !c.charging) return WorkloadPermission::Forbidden;

    if (c.battery_level_pct < 30 && !c.charging) return WorkloadPermission::Minimal;

    if (c.charging && !c.network_metered) return WorkloadPermission::Elevated;

    return WorkloadPermission::Normal;
}

WorkloadPermission ResourceController::permitted_workload() const {
    return evaluate(conditions());
}

bool ResourceController::may_crawl() const {
    return permitted_workload() != WorkloadPermission::Forbidden;
}

bool ResourceController::may_compact() const {
    const auto perm = permitted_workload();
    return perm == WorkloadPermission::Normal || perm == WorkloadPermission::Elevated;
}

} // namespace ali
