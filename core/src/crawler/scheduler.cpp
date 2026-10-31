// scheduler.cpp — Crawl scheduler.
//
// Coordinates the URL frontier, resource controller, and per-host politeness
// to produce a bounded stream of fetch-eligible URLs.
//
// Each call to run_cycle():
//   1. Checks the resource controller — returns 0 immediately if crawling
//      is not permitted (interactive search active, low battery, etc.).
//   2. Pops up to batch_size URLs from the frontier.
//   3. Invokes the caller-supplied FetchCallback for each URL.
//   4. Returns the number of URLs dispatched.
//
// The scheduler does not manage threads — concurrency is the caller's
// responsibility. One call to run_cycle() per crawler thread is typical.

#include "ali/crawler/scheduler.h"

#include <chrono>
#include <thread>

namespace ali {

Scheduler::Scheduler(UrlFrontier&        frontier,
                     ResourceController& resources,
                     SchedulerConfig     cfg)
    : frontier_(frontier), resources_(resources), cfg_(std::move(cfg)) {}

Scheduler::~Scheduler() = default;

uint32_t Scheduler::run_cycle(const FetchCallback& on_fetch,
                               int64_t              now_unix,
                               std::atomic<bool>&   cancel_flag) {
    if (cancel_flag.load()) return 0;

    // Check resource permission before doing any work.
    if (!resources_.may_crawl()) return 0;

    auto records = frontier_.pop(cfg_.batch_size, now_unix);
    if (records.empty()) {
        // Nothing eligible — sleep briefly to avoid a busy-wait loop.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(cfg_.idle_sleep_ms));
        return 0;
    }

    uint32_t dispatched = 0;
    for (const auto& rec : records) {
        if (cancel_flag.load()) break;
        // Re-check resource permission between each fetch dispatch.
        if (!resources_.may_crawl()) break;

        const bool keep_going = on_fetch(rec);
        ++dispatched;
        if (!keep_going) break;
    }
    return dispatched;
}

} // namespace ali
