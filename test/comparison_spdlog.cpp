// test/comparison_spdlog.cpp
#include <benchmark/benchmark.h>
#include <filesystem>
#include <iostream>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>
#include <string>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/types.h>
#include <unistd.h>
#elif __linux__
#include <fstream>
#endif

void PrintMemoryUsage(const std::string &label) {
#ifdef __APPLE__
  task_t task = mach_task_self();
  struct task_basic_info info;
  mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
  kern_return_t kerr =
      task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &size);

  if (kerr == KERN_SUCCESS) {
    double memory_mb =
        static_cast<double>(info.resident_size) / 1024.0 / 1024.0;
    std::cout << label << " - Memory: " << memory_mb << " MB (resident)"
              << std::endl;
  } else {
    std::cout << label << " - Memory: Unable to get memory info" << std::endl;
  }
#elif __linux__
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.find("VmRSS:") == 0) {
      std::cout << label << " - " << line << std::endl;
      break;
    }
  }
#else
  std::cout << label << " - Memory: Unsupported platform" << std::endl;
#endif
}

class LoggerBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    std::filesystem::create_directory(log_dir);
  }

  void TearDown(const ::benchmark::State &state) override {
    std::filesystem::remove_all(log_dir);
  }

protected:
  std::string log_dir = "./bench_spdlog";
};

class SpdlogFixture : public LoggerBenchmark {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("Before spdlog init " + name);
      LoggerBenchmark::SetUp(state);
      spdlog::shutdown(); // Clean up previous state
      spdlog::init_thread_pool(81920, 1);
      auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
          "async_logger", log_dir + "/log.txt", true /* truncate */);
      spdlog::set_default_logger(std::move(logger));
      PrintMemoryUsage("After spdlog init " + name);
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("Before spdlog shutdown " + name);
      spdlog::shutdown();
      LoggerBenchmark::TearDown(state);
      PrintMemoryUsage("After spdlog shutdown " + name);
    }
  }
};

// ============================================================
// 1. True Throughput Test (Single and Multi-threaded)
// ============================================================
BENCHMARK_F(SpdlogFixture, Throughput)(benchmark::State &state) {
  constexpr int messages_per_iteration = 10000;
  for (auto _ : state) {
    for (int i = 0; i < messages_per_iteration; ++i) {
      spdlog::info("Thread {} message {}", state.thread_index(), i);
    }
    spdlog::default_logger()->flush();
  }
  state.SetItemsProcessed(state.iterations() * messages_per_iteration);
}
BENCHMARK_REGISTER_F(SpdlogFixture, Throughput)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8);

// ============================================================
// 2. Latency Test (Time to push to the queue)
// ============================================================
BENCHMARK_F(SpdlogFixture, Latency)(benchmark::State &state) {
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    spdlog::info("Latency test message {}", state.iterations());
    auto end = std::chrono::high_resolution_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - start).count());
  }
}
BENCHMARK_REGISTER_F(SpdlogFixture, Latency)->UseManualTime();

// ============================================================
// 3. Formatting-Only Performance Test
// ============================================================
BENCHMARK_F(SpdlogFixture, Formatting)(benchmark::State &state) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  spdlog::logger logger("null_logger", null_sink);
  logger.set_pattern("%v");

  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      logger.info("Int: {}, Float: {}, String: {}, Bool: {}", i, 3.14159 * i,
                  "test string", i % 2 == 0);
    }
  }
  state.SetItemsProcessed(state.iterations() * 100);
}

// ============================================================
// 4. Payload Performance Test
// ============================================================
BENCHMARK_DEFINE_F(SpdlogFixture, Payload)(benchmark::State &state) {
  const size_t messageCount = 1000;
  const size_t messageSize = state.range(0);
  const std::string largeMessage(messageSize, 'X');
  for (auto _ : state) {
    for (size_t i = 0; i < messageCount; ++i) {
      spdlog::info("Large payload {}: {}", i, largeMessage);
    }
    spdlog::default_logger()->flush();
  }
  state.SetBytesProcessed(state.iterations() * messageCount * messageSize);
}
BENCHMARK_REGISTER_F(SpdlogFixture, Payload)->Range(1024, 1024 * 8);

BENCHMARK_MAIN();