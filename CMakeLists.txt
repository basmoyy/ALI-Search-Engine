cmake_minimum_required(VERSION 3.22)
project(ALI VERSION 0.1.0 LANGUAGES CXX)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ── Options ──────────────────────────────────────────────────────────────────
option(ALI_BUILD_TESTS      "Build unit and integration tests"  ON)
option(ALI_BUILD_BENCHMARKS "Build benchmark harness"           ON)
option(ALI_BUILD_TOOLS      "Build command-line developer tools" ON)
option(ALI_ENABLE_ASAN      "Enable AddressSanitizer"           OFF)
option(ALI_ENABLE_TSAN      "Enable ThreadSanitizer"            OFF)

# ── Compiler flags ───────────────────────────────────────────────────────────
add_library(ali_compile_options INTERFACE)
target_compile_options(ali_compile_options INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wno-unused-parameter
  >
  $<$<CXX_COMPILER_ID:MSVC>:
    /W4 /permissive-
  >
)

if(ALI_ENABLE_ASAN)
  target_compile_options(ali_compile_options INTERFACE -fsanitize=address,undefined)
  target_link_options(ali_compile_options INTERFACE -fsanitize=address,undefined)
endif()

if(ALI_ENABLE_TSAN)
  target_compile_options(ali_compile_options INTERFACE -fsanitize=thread)
  target_link_options(ali_compile_options INTERFACE -fsanitize=thread)
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(SQLite3 REQUIRED)

# ── Core library ─────────────────────────────────────────────────────────────
add_subdirectory(core)

# ── C API shared/static library ──────────────────────────────────────────────
add_subdirectory(interfaces/c_api)

# ── Optional sub-projects ────────────────────────────────────────────────────
if(ALI_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(ALI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(ALI_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
