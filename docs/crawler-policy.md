# ALI Crawler Policy

## Access-aware discovery (v0.3)

The crawler must not equate a blocked URL with a nonexistent one. Classify every failed or denied fetch into the correct lifecycle state so persistent scheduling state survives restarts.

- `Restricted` — set when robots.txt, policy rules, or access controls block retrieval. Do not retry without a policy change.
- `AuthRequired` — set when a login wall, paywall, or session requirement is detected. Do not bypass.
- `RateLimited` — set when a 429 or equivalent backoff is received. Schedule retry after the indicated or default delay.
- `TemporarilyUnavailable` — set on transient network, DNS, or server failures. Apply exponential backoff.
- `Removed` — set when content is explicitly deleted or withdrawn from ALI.

Do not bypass CAPTCHA, Cloudflare challenges, login controls, paywalls, or other technical access controls.

---



- Respect `robots.txt` for every host. Fetch and cache rules before crawling.
- Enforce per-host minimum request delay (default: 1000 ms).
- Limit concurrent fetches to 2 by default.
- Apply exponential backoff after fetch failures.
- Limit response size (default: 2 MB).
- Limit redirect depth (default: 5).
- Never fetch private-network, loopback or link-local addresses (SSRF defence).

## Resource-aware scheduling

| Condition              | Response                              |
|------------------------|---------------------------------------|
| No network             | Pause fetches; local search continues |
| Metered network        | Pause unless user explicitly opted in |
| Battery < 15%          | Pause (unless charging)               |
| Charging + unmetered   | Elevated crawl budget                 |
| Memory/thermal pressure | Stop immediately                     |
| Interactive search     | Crawler yields; query has priority    |
| Low storage            | Stop writes; surface alert to user    |

## Data and privacy

- All crawl data stays on the device by default.
- Original HTML, images and scripts are not retained.
- Users may configure crawl scope, domain exclusions and data limits.
- Users may delete any or all crawled data at any time.
- Search history does not leave the device.

## Platform reality

- Android and iOS constrain background execution.
- Do not promise continuous crawling.
- Design all crawl jobs as resumable and checkpointed.
- Android: use WorkManager or foreground services per current platform rules.
- iOS: background execution is more restricted; defer to platform guidance.
