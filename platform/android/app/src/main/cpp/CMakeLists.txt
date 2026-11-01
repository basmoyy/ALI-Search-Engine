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
    # Suppress warnings from third-party SQLite amalgamation
    -Wno-shorten-64-to-32
)
target_compile_definitions(ali_compile_options INTERFACE
    # Disable OpenSSL — Android handles HTTPS via platform shell
    ALI_DISABLE_OPENSSL=1
    # Suppress SQLite thread-safety warnings
    SQLITE_THREADSAFE=1
)

# ── SQLite amalgamation (bundled — NDK does not include sqlite3.h) ────────────
# Using the official SQLite amalgamation (single .c + .h pair).
# This is the standard approach for Android NDK builds.
set(SQLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sqlite")

add_library(sqlite3_bundled STATIC
    "${SQLITE_DIR}/sqlite3.c"
)
target_include_directories(sqlite3_bundled PUBLIC "${SQLITE_DIR}")
# SQLite compiles as C, not C++ — tell the compiler explicitly
set_source_files_properties("${SQLITE_DIR}/sqlite3.c"
    PROPERTIES LANGUAGE C
)

# Provide the CMake target the core expects
add_library(SQLite3::SQLite3 ALIAS sqlite3_bundled)

# ── OpenSSL stubs (HTTPS handled by Android platform shell) ──────────────────
add_library(OpenSSL::SSL    INTERFACE IMPORTED)
add_library(OpenSSL::Crypto INTERFACE IMPORTED)

# ── Core library ──────────────────────────────────────────────────────────────
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
    "${SQLITE_DIR}"
)

target_link_libraries(ali_core PUBLIC
    sqlite3_bundled
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
    "${SQLITE_DIR}"
)

target_link_libraries(ali_capi PUBLIC ali_core)

# ── JNI shared library ────────────────────────────────────────────────────────
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

set_target_properties(ali_jni PROPERTIES OUTPUT_NAME "ali_core")
