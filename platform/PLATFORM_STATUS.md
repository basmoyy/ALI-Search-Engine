# ALI Platform Status

ALI uses **one shared C++ core** and **one stable C API** (`interfaces/c_api/ali.h`).
Each platform has an independent native shell. Nothing in the core is
platform-specific. The same index format and search behaviour apply on all platforms.

---

## Android
**Status: Buildable — awaiting NDK build and device test**

| Component | Status |
|---|---|
| C++ core (via NDK CMake bridge) | ✅ Structure complete |
| C API (`ali.h`) | ✅ Stable |
| JNI bridge (`AliJni.cpp`) | ✅ Complete |
| Kotlin engine wrapper (`AliEngine.kt`) | ✅ Complete |
| Resource controller (`AliResourceController.kt`) | ✅ Complete |
| Storage paths (`AliStoragePaths.kt`) | ✅ Complete |
| WorkManager crawler job | ✅ Complete |
| Foreground crawl service | ✅ Complete |
| Compose UI: Search, Index, Benchmark screens | ✅ Complete |
| Gradle project files | ✅ Complete |
| NDK build tested | ❌ Requires Android Studio + NDK |
| Device test on low-end hardware | ❌ Required for v0.1 claim |

**To build:**
```sh
cd platform/android
./gradlew assembleDebug
```
Requires: Android Studio, NDK 26+, API 26+ device or emulator.

---

## iOS
**Status: Adapter stub — not yet tested**

| Component | Status |
|---|---|
| C++ core (shared, cross-compiled) | Pending Xcode project |
| Swift C interop boundary | Stub (`AliPlatformIOS.h/.cpp`) |
| Background App Refresh integration | Not started |
| UI (SwiftUI) | Not started |

Do not claim iOS support until the core builds for iOS, C interop tests pass,
and lifecycle tests run on a real device.

---

## macOS
**Status: Adapter stub — not yet tested**

| Component | Status |
|---|---|
| C++ core | Builds on macOS (development platform) |
| Platform adapter | Stub (`AliPlatformMacOS.h/.cpp`) |
| Qt or SwiftUI shell | Not started |

---

## Windows
**Status: Adapter stub — not yet tested**

| Component | Status |
|---|---|
| C++ core | Portable (no Windows-specific code in core) |
| Platform adapter | Stub (`AliPlatformWindows.h/.cpp`) |
| Qt/C++ or C# shell | Not started |

---

## Linux
**Status: Adapter stub — not yet tested**

| Component | Status |
|---|---|
| C++ core | Portable |
| Platform adapter | Stub (`AliPlatformLinux.h/.cpp`) |
| Qt/C++ shell | Not started |

---

## What is shared across all platforms (must never become platform-specific)

- `core/` — all search, index, scanner, ranking, compression, locality, crawler logic
- `interfaces/c_api/ali.h` — the stable C ABI boundary
- Index binary format (segment files + manifest)
- Query semantics (BM25, Block-Max WAND)
- Resource budget (12 MB working-set, selective block retrieval)
- Robots.txt politeness and crawl safety rules
