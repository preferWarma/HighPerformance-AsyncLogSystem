add_executable(log_benchmark
  log_benchmark.cpp
)

target_include_directories(log_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(log_benchmark PRIVATE benchmark::benchmark)
