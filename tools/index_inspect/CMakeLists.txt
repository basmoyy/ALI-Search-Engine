add_executable(ali_inspect main.cpp)
target_link_libraries(ali_inspect PRIVATE ali_core ali_compile_options)
target_include_directories(ali_inspect PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
