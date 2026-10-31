add_library(ali_capi STATIC
  ali_engine.cpp
)

target_include_directories(ali_capi
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(ali_capi
  PUBLIC  ali_core
  PRIVATE ali_compile_options
)
