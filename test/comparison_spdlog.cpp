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

// ============================================================
// Base Fixture for All Logger Benchmarks
// ============================================================
class LoggerBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    // Create directories for logs before any test runs
    std::filesystem::create_directory("./bench_spdlog");
  }

  void TearDown(const ::benchmark::State &state) override {
    // Optional: Clean up log directories after all tests are done
    // std::filesystem::remove_all("./bench_spdlog");
  }
};

// ============================================================
// Fixture for Spdlog Asynchronous Logger
// ============================================================
class SpdlogFixture : public LoggerBenchmark {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("Before " + name);
      LoggerBenchmark::SetUp(state);
      // Ensure a clean state from any previous runs that might have failed.
      spdlog::shutdown();
      spdlog::drop_all();

      // This setup is more robust for repeated runs within the same process
      spdlog::init_thread_pool(81920, 1); // Match queue size
      auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
          "async_logger", "./bench_spdlog/log.txt", true /* truncate */);
      logger->set_level(spdlog::level::debug);
      logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
      spdlog::set_default_logger(std::move(logger));
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      spdlog::shutdown();
      spdlog::drop_all();
      LoggerBenchmark::TearDown(state);
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("After " + name);
    }
  }
};

// ============================================================
// 1. Throughput Test (Single and Multi-threaded)
// ============================================================
BENCHMARK_F(SpdlogFixture, Throughput)(benchmark::State &state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      spdlog::info("Thread {} message {}", state.thread_index(), i);
    }
  }
  state.SetItemsProcessed(state.iterations() * 100);
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
    spdlog::info("Latency test message");
    auto end = std::chrono::high_resolution_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - start).count());
  }
}
BENCHMARK_REGISTER_F(SpdlogFixture, Latency)->UseManualTime();

// ============================================================
// 3. Formatting-Only Performance Test
// ============================================================

// A separate fixture for tests that don't need full logger setup
class FormattingFixture : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("Before " + name);
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("After " + name);
    }
  }
};

BENCHMARK_F(FormattingFixture, Spdlog_Formatting)(benchmark::State &state) {
  auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  spdlog::logger logger("null_logger", null_sink);
  logger.set_pattern("%v"); // Only format the message, no extra pattern

  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      // Using logger.info on a null_sink is a standard way to test formatting
      // performance
      logger.info("Int: {}, Float: {}, String: {}, Bool: {}", i, 3.14159 * i,
                  "test string", i % 2 == 0);
    }
  }
  state.SetItemsProcessed(state.iterations() * 100);
}

// --- Spdlog Tests ---
static void RunSpdlogPayload(benchmark::State &state, size_t messageSize) {
  const size_t messageCount = 1000;
  std::string largeMessage(messageSize, 'X');
  for (auto _ : state) {
    for (size_t i = 0; i < messageCount; ++i) {
      spdlog::info("Large payload {}: {}", i, largeMessage);
    }
  }
  state.SetBytesProcessed(state.iterations() * messageCount * messageSize);
}

BENCHMARK_F(SpdlogFixture, LargePayload_1KB)(benchmark::State &state) {
  RunSpdlogPayload(state, 1024);
}

BENCHMARK_F(SpdlogFixture, LargePayload_8KB)(benchmark::State &state) {
  RunSpdlogPayload(state, 8192);
}

BENCHMARK_F(SpdlogFixture, LargePayload_16KB)(benchmark::State &state) {
  RunSpdlogPayload(state, 16384);
}

BENCHMARK_MAIN();