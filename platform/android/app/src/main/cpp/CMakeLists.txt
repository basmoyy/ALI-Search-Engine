cmake_minimum_required(VERSION 3.22)
project(ali_android_bridge)

# ── Locate the repo root ──────────────────────────────────────────────────────
# This file lives at platform/android/app/src/main/cpp/CMakeLists.txt.
# The repo root is five directories up.
get_filename_component(REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.."
    ABSOLUTE)

message(STATUS "ALI repo root: ${REPO_ROOT}")

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler options ──────────────────────────────────────────────────────────
add_library(ali_compile_options INTERFACE)
target_compile_options(ali_compile_options INTERFACE
    -Wall -Wextra -Wno-unused-parameter
)

# ── SQLite (bundled with Android NDK) ────────────────────────────────────────
# Android API 26+ includes libsqlite.so in the NDK sysroot.
# We link against the system library rather than bundling our own copy.
find_library(SQLITE_LIB sqlite3)
if(NOT SQLITE_LIB)
    # Fallback: compile SQLite amalgamation from Android SDK sources.
    message(STATUS "System sqlite3 not found, using NDK bundled version")
    set(SQLITE_LIB "sqlite3")
endif()

# Provide a CMake target that the core can link against.
add_library(SQLite3::SQLite3 INTERFACE IMPORTED)
set_target_properties(SQLite3::SQLite3 PROPERTIES
    INTERFACE_LINK_LIBRARIES "${SQLITE_LIB}"
)

# ── OpenSSL (bundled via prefab or static build) ──────────────────────────────
# For the Android harness, we disable the TLS fetcher and use
# Android's built-in HttpsURLConnection via a JNI callback instead.
# This avoids the OpenSSL cross-compilation complexity for the initial harness.
# The fetcher gracefully returns FetchResultCode::NetworkError for HTTPS
# when OpenSSL is unavailable — the Android shell handles networking.
#
# When OpenSSL prefab support is confirmed, set:
#   find_package(openssl REQUIRED CONFIG)
# For now, provide a no-op stub target.
add_library(OpenSSL::SSL    INTERFACE IMPORTED)
add_library(OpenSSL::Crypto INTERFACE IMPORTED)

# Disable the TLS code path for this Android build.
target_compile_definitions(ali_compile_options INTERFACE
    ALI_DISABLE_OPENSSL=1
)

# ── Core library ──────────────────────────────────────────────────────────────
# Include all core source files directly. We do not use add_subdirectory
# because the root CMakeLists.txt has desktop-specific settings that
# conflict with the Android toolchain. Instead we enumerate sources here,
# keeping the core itself completely unchanged.

file(GLOB_RECURSE CORE_SOURCES
    "${REPO_ROOT}/core/src/storage/*.cpp"
    "${REPO_ROOT}/core/src/compression/*.cpp"
    "${REPO_ROOT}/core/src/index/*.cpp"
    "${REPO_ROOT}/core/src/query/*.cpp"
    "${REPO_ROOT}/core/src/scanner/*.cpp"
    "${REPO_ROOT}/core/src/ranking/*.cpp"
    "${REPO_ROOT}/core/src/locality/*.cpp"
    "${REPO_ROOT}/core/src/crawler/*.cpp"
)

add_library(ali_core STATIC ${CORE_SOURCES})

target_include_directories(ali_core PUBLIC
    "${REPO_ROOT}/core/include"
)

target_link_libraries(ali_core PUBLIC
    SQLite3::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    android
    log
)

target_link_libraries(ali_core PRIVATE ali_compile_options)

# ── C API ─────────────────────────────────────────────────────────────────────

add_library(ali_capi STATIC
    "${REPO_ROOT}/interfaces/c_api/ali_engine.cpp"
)

target_include_directories(ali_capi PUBLIC
    "${REPO_ROOT}/interfaces/c_api"
)

target_link_libraries(ali_capi PUBLIC ali_core)

# ── JNI shared library (loaded by System.loadLibrary("ali_core")) ─────────────
# The JNI bridge is the only Android-specific native code.
# Everything else (core, c_api) is cross-platform.

add_library(ali_jni SHARED
    "${REPO_ROOT}/platform/android/app/src/main/jni/AliJni.cpp"
)

target_include_directories(ali_jni PRIVATE
    "${REPO_ROOT}/interfaces/c_api"
    "${REPO_ROOT}/core/include"
)

target_link_libraries(ali_jni PRIVATE
    ali_capi
    android
    log
)

# The library must be named "ali_core" to match System.loadLibrary("ali_core").
set_target_properties(ali_jni PROPERTIES OUTPUT_NAME "ali_core")
