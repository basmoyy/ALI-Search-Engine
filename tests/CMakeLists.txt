include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ── Unit tests ────────────────────────────────────────────────────────────────
add_executable(ali_unit_tests
  unit/test_vbyte.cpp
  unit/test_delta.cpp
  unit/test_bm25.cpp
  unit/test_result_set.cpp
  unit/test_tokeniser.cpp
  unit/test_locality_memory.cpp
  unit/test_segment_write_read.cpp
  unit/test_manifest.cpp
  unit/test_query_planner.cpp
  unit/test_scanner.cpp
)

target_link_libraries(ali_unit_tests
  PRIVATE ali_core ali_compile_options
          GTest::gtest_main
)

target_include_directories(ali_unit_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/core/include
          ${CMAKE_SOURCE_DIR}/tests/fixtures
)

include(GoogleTest)
gtest_discover_tests(ali_unit_tests)

# ── Integration tests ────────────────────────────────────────────────────────
add_executable(ali_integration_tests
  integration/test_index_search_round_trip.cpp
  integration/test_multi_segment.cpp
)

target_link_libraries(ali_integration_tests
  PRIVATE ali_core ali_capi ali_compile_options
          GTest::gtest_main
)

target_include_directories(ali_integration_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/core/include
          ${CMAKE_SOURCE_DIR}/interfaces/c_api
          ${CMAKE_SOURCE_DIR}/tests/fixtures
)

gtest_discover_tests(ali_integration_tests)

# ── Recovery tests ────────────────────────────────────────────────────────────
add_executable(ali_recovery_tests
  recovery/test_crash_recovery.cpp
)

target_link_libraries(ali_recovery_tests
  PRIVATE ali_core ali_compile_options GTest::gtest_main
)

target_include_directories(ali_recovery_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/core/include
)

gtest_discover_tests(ali_recovery_tests)
