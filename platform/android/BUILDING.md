# Building and Validating ALI on Android

This document covers everything needed to build the ALI Android app,
install it on a physical device, and run the hardware validation benchmark.

---

## Prerequisites

### 1. Android Studio

Download from https://developer.android.com/studio and install.

### 2. Android NDK

In Android Studio:
- Open **SDK Manager** (Tools → SDK Manager)
- Click **SDK Tools** tab
- Check **NDK (Side by side)** — install version **26.3** or later
- Check **CMake** — install version **3.22.1** or later
- Click **Apply**

Verify:
```sh
ls $HOME/Library/Android/sdk/ndk/
# Should show: 26.3.11579264 (or similar)
```

### 3. Java 11+

Android Studio ships with a bundled JDK. If building from the command line:
```sh
java -version  # must be 11 or later
```

### 4. A physical Android device (recommended)

- Android 8.0+ (API 26+)
- Enable **Developer Options**: Settings → About Phone → tap Build Number 7 times
- Enable **USB Debugging**: Developer Options → USB Debugging → ON
- Connect via USB and confirm the prompt on the device

The emulator works for functional testing but will not give accurate
latency or memory numbers for the benchmark.

---

## Build

### Option A — Android Studio (recommended for first build)

1. Open Android Studio
2. **File → Open** → navigate to `ALI/platform/android`
3. Wait for Gradle sync to complete (downloads dependencies, ~2 min first time)
4. Select your device in the device dropdown (top toolbar)
5. Click **Run** (green triangle) or press **Shift+F10**

### Option B — Command line

```sh
cd ALI/platform/android

# Debug build (faster, no obfuscation)
./gradlew assembleDebug

# Install directly to connected device
./gradlew installDebug

# Or build a release APK (requires signing config)
./gradlew assembleRelease
```

The APK is at:
```
app/build/outputs/apk/debug/app-debug.apk
```

### Option C — ADB install (if you already have the APK)

```sh
adb install app/build/outputs/apk/debug/app-debug.apk
```

---

## What the NDK build does

The `app/src/main/cpp/CMakeLists.txt` file:

1. Locates the repo root (five directories up from the CMake file)
2. Compiles every `.cpp` file in `core/src/` for the Android ABI
3. Compiles `interfaces/c_api/ali_engine.cpp`
4. Wraps `platform/android/app/src/main/jni/AliJni.cpp` into `libali_core.so`

**The core C++ source is not modified or copied.** It is compiled in-place
from the repo, keeping one canonical codebase for all platforms.

---

## Validating on device

### 1. Import a test corpus

The app doesn't ship with a corpus. You have two options:

**Option A — Use the crawler** (simplest)
1. Open the app → tap **Index**
2. Enter a seed URL (e.g. `https://en.wikipedia.org/wiki/Search_engine`)
3. Tap **Start**
4. The crawler will discover and index pages in the background

**Option B — Push a pre-built corpus via ADB**
```sh
# Generate a small corpus on your Mac
cd ALI
python3 tools/corpus_import/gen_corpus.py 5000 /tmp/corpus_5k.tsv

# Push to device
adb push /tmp/corpus_5k.tsv /sdcard/Download/corpus_5k.tsv

# The app's import screen can read from the Downloads folder
# (or use adb shell to run ali_import if you've built the CLI for Android)
```

### 2. Run the benchmark

1. Open the app → tap **Benchmark**
2. The **Device** card shows your device model, Android version and ABI
3. Tap **Run Benchmark**
4. Wait ~30 seconds (50 queries × 5 iterations)
5. Results show p50/p95/p99 latency and PSS memory delta
6. Tap **Copy report to clipboard** to capture the raw numbers

### 3. What to look for

| Metric | Target | Pass |
|--------|--------|------|
| p95 latency | < 100 ms | ✅ if green |
| PSS delta | < 12 MB | ✅ if green |
| p99 latency | < 200 ms | acceptable on low-end |

If p95 > 100 ms on a Cortex-A55 device, that's real data — record it
and report it. Do not adjust the threshold to make the test pass.

### 4. Capture the full device report

The benchmark screen's "Copy report" button produces:

```
=== ALI Benchmark Report ===
Device: [Manufacturer] [Model]
Android: API XX (Y.Z)
ABI: arm64-v8a

--- device_warm ---
docs=10000
p50=Xms  p95=Yms  p99=Zms
pss_delta=NKB
```

Paste this into `ALI/benchmarks/reports/` as a new `.md` file following
the naming convention `YYYY-MM-DD_<device>_<corpus_size>.md`.

---

## Troubleshooting

**Gradle sync fails — NDK not found**
```
Set ANDROID_NDK_ROOT in local.properties:
echo "ndk.dir=$HOME/Library/Android/sdk/ndk/26.3.11579264" >> local.properties
```

**`System.loadLibrary("ali_core")` crashes at runtime**
- Check `adb logcat` for the full error
- Most common: ABI mismatch (x86_64 emulator but arm64 build)
- In `app/build.gradle.kts`, verify `abiFilters` includes the device ABI

**OpenSSL linker errors**
The Android build uses `ALI_DISABLE_OPENSSL=1` — HTTPS is handled by
Android's built-in `HttpsURLConnection` via the platform shell.
If you see OpenSSL errors, check that the CMake define is being passed.

**Build succeeds but search returns no results**
The index is empty until you import a corpus or run the crawler.
Check the Index screen — doc count should be > 0 before benchmarking.

---

## ADB useful commands

```sh
# Check connected devices
adb devices

# View app logs in real time
adb logcat -s ALI

# Check memory usage
adb shell dumpsys meminfo com.ali.search

# Check if the native library loaded
adb shell cat /proc/$(adb shell pidof com.ali.search)/maps | grep ali_core

# Pull the index off the device for inspection
adb pull /data/data/com.ali.search/files/ali/ /tmp/ali_device_index/
```

---

## Next steps after device validation

Once the benchmark numbers are recorded:

1. Add them to `benchmarks/reports/` with the device model
2. If p95 < 100 ms and PSS delta < 12 MB: **v0.1 Android acceptance criteria met**
3. Begin iOS port using the same C API (`ali.h`) — zero core changes needed
4. Begin Qt desktop UI for macOS/Windows/Linux
