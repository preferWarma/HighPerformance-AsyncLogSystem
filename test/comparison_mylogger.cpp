#include <benchmark/benchmark.h>
#include <filesystem>
#include <string>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/types.h>
#include <unistd.h>
#elif __linux__
#include <fstream>
#endif

#include "Helper.h"
#include "LogSystem.h"

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
    std::filesystem::create_directory("./bench_your_log");
  }

  void TearDown(const ::benchmark::State &state) override {
    // Optional: Clean up log directories after all tests are done
    // std::filesystem::remove_all("./bench_your_log");
  }
};

// ============================================================
// Fixture for Your Asynchronous Logger
// ============================================================
class YourLoggerFixture : public LoggerBenchmark {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("Before " + name);
      LoggerBenchmark::SetUp(state);
      auto &cfg = lyf::Config::GetInstance();
      cfg.output.toConsole = false;
      cfg.output.toFile = true;
      cfg.output.logRootDir = "./bench_your_log";
      cfg.cloud.enable = false;
      cfg.performance.fileFlushInterval = std::chrono::milliseconds(1000);
      cfg.performance.fileBatchSize = 4096;
      cfg.basic.maxFileLogQueueSize = 81920; // Increased queue size

      lyf::AsyncLogSystem::GetInstance().Init();
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      lyf::AsyncLogSystem::GetInstance().Stop();
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
BENCHMARK_F(YourLoggerFixture, Throughput)(benchmark::State &state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      LOG_INFO("Thread {} message {}", state.thread_index(), i);
    }
  }
  state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK_REGISTER_F(YourLoggerFixture, Throughput)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8);

// ============================================================
// 2. Latency Test (Time to push to the queue)
// ============================================================
BENCHMARK_F(YourLoggerFixture, Latency)(benchmark::State &state) {
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Latency test message");
    auto end = std::chrono::high_resolution_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - start).count());
  }
}
BENCHMARK_REGISTER_F(YourLoggerFixture, Latency)->UseManualTime();

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

BENCHMARK_F(FormattingFixture, YourLogger_Formatting)(benchmark::State &state) {
  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      benchmark::DoNotOptimize(
          lyf::FormatMessage("Int: {}, Float: {}, String: {}, Bool: {}", i,
                             3.14159 * i, "test string", i % 2 == 0));
    }
  }
  state.SetItemsProcessed(state.iterations() * 100);
}

// --- YourLogger Tests ---
static void RunYourLoggerPayload(benchmark::State &state, size_t messageSize) {
  const size_t messageCount = 1000;
  std::string largeMessage(messageSize, 'X');
  for (auto _ : state) {
    for (size_t i = 0; i < messageCount; ++i) {
      LOG_INFO("Large payload {}: {}", i, largeMessage);
    }
  }
  state.SetBytesProcessed(state.iterations() * messageCount * messageSize);
}

BENCHMARK_F(YourLoggerFixture, LargePayload_1KB)(benchmark::State &state) {
  RunYourLoggerPayload(state, 1024);
}

BENCHMARK_F(YourLoggerFixture, LargePayload_8KB)(benchmark::State &state) {
  RunYourLoggerPayload(state, 8192);
}

BENCHMARK_F(YourLoggerFixture, LargePayload_16KB)(benchmark::State &state) {
  RunYourLoggerPayload(state, 16384);
}

BENCHMARK_MAIN();