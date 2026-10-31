add_executable(ali_import main.cpp)
target_link_libraries(ali_import PRIVATE ali_core ali_capi ali_compile_options)
target_include_directories(ali_import
  PRIVATE ${CMAKE_SOURCE_DIR}/core/include
          ${CMAKE_SOURCE_DIR}/interfaces/c_api)
