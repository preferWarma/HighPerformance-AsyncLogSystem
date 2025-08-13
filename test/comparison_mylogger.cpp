#include "Config.h"
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

class LoggerBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    std::filesystem::create_directory(log_dir);
  }

  void TearDown(const ::benchmark::State &state) override {
    std::filesystem::remove_all(log_dir);
  }

protected:
  std::string log_dir = "./bench_mylogger";
};

class MyLoggerFixture : public LoggerBenchmark {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }

      PrintMemoryUsage("Before AsyncLogSystem Init " + name);
      LoggerBenchmark::SetUp(state);
      auto &cfg = lyf::Config::GetInstance();
      cfg.output.toConsole = false;
      cfg.output.toFile = true;
      cfg.output.logRootDir = log_dir;
      cfg.cloud.enable = false;
      cfg.basic.maxQueueSize = 1024000; // 限制队列长度减少内存占用
      cfg.basic.queueFullPolicy = lyf::QueueFullPolicy::BLOCK;
      lyf::AsyncLogSystem::GetInstance().Init();
      PrintMemoryUsage("After AsyncLogSystem Init " + name);
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      std::string name = state.name();
      if (name.find("threads:") == std::string::npos) {
        name += "/threads:" + std::to_string(state.threads());
      }
      PrintMemoryUsage("Before AsyncLogSystem Stop " + name);
      lyf::AsyncLogSystem::GetInstance().Stop();
      LoggerBenchmark::TearDown(state);
      PrintMemoryUsage("After AsyncLogSystem Stop " + name);
    }
  }
};

// ============================================================
// 1. True Throughput Test (Single and Multi-threaded)
// 端到端吞吐量测试，从LOG_XXX到写入磁盘
// ============================================================
BENCHMARK_F(MyLoggerFixture, Throughput)(benchmark::State &state) {
  // 每个iteration写10000条消息
  constexpr int messages_per_iteration = 10000;

  for (auto _ : state) {
    // The logging work itself.
    for (int i = 0; i < messages_per_iteration; ++i) {
      LOG_INFO("Thread {} message {}", state.thread_index(), i);
    }
    // 每个iteration手动flush，保证写入磁盘
    lyf::AsyncLogSystem::GetInstance().Flush();
  }

  // 记录写入的消息数量
  state.SetItemsProcessed(state.iterations() * messages_per_iteration);
}
BENCHMARK_REGISTER_F(MyLoggerFixture, Throughput)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8);

// ============================================================
// 2. Latency Test (Time to push to the queue)
// 队列推送延迟测试
// ============================================================
BENCHMARK_F(MyLoggerFixture, Latency)(benchmark::State &state) {
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Latency test message {}", state.iterations());
    auto end = std::chrono::high_resolution_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - start).count());
  }
}
BENCHMARK_REGISTER_F(MyLoggerFixture, Latency)->UseManualTime();

// ============================================================
// 3. Formatting-Only Performance Test
// 格式化性能测试
// ============================================================
BENCHMARK_F(MyLoggerFixture, Formatting)(benchmark::State &state) {
  // 关闭日志输出，只测试格式化性能
  lyf::Config &cfg = lyf::Config::GetInstance();
  cfg.output.toConsole = false;
  cfg.output.toFile = false;

  constexpr char fmt[] = "Int: {}, Float: {}, String: {}, Bool: {}";

  for (auto _ : state) {
    for (int i = 0; i < 100; ++i) {
      benchmark::DoNotOptimize(
#if _LIBCPP_STD_VER >= 20
          // 编译期优化后的格式化
          lyf::FormatMessage<fmt>(i, 3.14159 * i, "test string", i % 2 == 0)
#else
          // 仅运行时格式化
          lyf::FormatMessage(fmt, i, 3.14159 * i, "test string", i % 2 == 0)
#endif
      );
    }
  }
  state.SetItemsProcessed(state.iterations() * 100);
}

// ============================================================
// 4. Payload Performance Test
// 负载性能测试
// ============================================================
BENCHMARK_F(MyLoggerFixture, Payload)(benchmark::State &state) {
  const size_t messageCount = 1000;
  const size_t messageSize = 1024;
  const std::string largeMessage(messageSize, 'X');
  for (auto _ : state) {
    for (size_t i = 0; i < messageCount; ++i) {
      LOG_INFO("Large payload {}: {}", i, largeMessage);
    }
    lyf::AsyncLogSystem::GetInstance().Flush();
  }
  state.SetBytesProcessed(state.iterations() * messageCount * messageSize);
}
BENCHMARK_REGISTER_F(MyLoggerFixture, Payload)->Range(1024, 1024 * 8);

BENCHMARK_MAIN();