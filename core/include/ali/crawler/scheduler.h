#pragma once
// ali/crawler/scheduler.h — Crawl scheduler.
//
// Coordinates the frontier, resource controller and per-host politeness
// to produce a stream of fetch-eligible URLs within device resource limits.

#include "ali/crawler/url_frontier.h"
#include "ali/crawler/resource_controller.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ali {

struct SchedulerConfig {
    uint32_t max_concurrent_fetches = 2;
    uint32_t batch_size             = 10;  ///< URLs popped from frontier per cycle
    uint32_t idle_sleep_ms          = 500; ///< sleep when no URLs are eligible
};

/// Callback invoked by the scheduler for each URL to fetch.
/// Return true to continue scheduling; false to stop.
using FetchCallback = std::function<bool(const UrlRecord&)>;

class Scheduler {
public:
    explicit Scheduler(UrlFrontier&        frontier,
                       ResourceController& resources,
                       SchedulerConfig     cfg = {});
    ~Scheduler();

    /// Run one scheduling cycle: check resource permission, pop eligible URLs,
    /// invoke callback for each. Returns number of URLs dispatched.
    uint32_t run_cycle(const FetchCallback& on_fetch,
                       int64_t              now_unix,
                       std::atomic<bool>&   cancel_flag);

private:
    UrlFrontier&        frontier_;
    ResourceController& resources_;
    SchedulerConfig     cfg_;
};

} // namespace ali
