// log_benchmark.cpp
#include "LogSystem.h"
#include <benchmark/benchmark.h>
#include <vector>

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

using namespace lyf;

// -------------------------------
// 基础工具
// -------------------------------
static void SetUpTest(const std::string &logDir) {
  auto &cfg = Config::GetInstance();
  cfg.Init();
  cfg.output.logRootDir = logDir;
  cfg.output.toConsole = false;
  cfg.output.toFile = true;
  cfg.output.minLogLevel = 0;
  cfg.cloud.enable = false; // 禁用云上传,只对日志写入文件的情况进行压力测试
}

static void TearDownTest(const std::string &logDir) {
  std::error_code ec;
  std::filesystem::remove_all(logDir, ec);
}

// -------------------------------
// 单线程吞吐量
// -------------------------------
static void BM_SingleThreadThroughput(benchmark::State &state) {
  const std::string logDir = "bench_single";
  SetUpTest(logDir);

  AsyncLogSystem::GetInstance().Init();

  for (auto _ : state) {
    state.PauseTiming();
    const size_t kMessages = state.range(0);
    state.ResumeTiming();

    for (size_t i = 0; i < kMessages; ++i) {
      LOG_INFO("msg {}", i);
    }
  }

  AsyncLogSystem::GetInstance().Stop();

  state.SetItemsProcessed(state.iterations() * state.range(0));
  TearDownTest(logDir);
}
BENCHMARK(BM_SingleThreadThroughput)->RangeMultiplier(10)->Range(1000, 1000000);

// -------------------------------
// 多线程并发
// -------------------------------
class MultiThreadFixture : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      logDir = "bench_multi";
      SetUpTest(logDir);
      AsyncLogSystem::GetInstance().Init();
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      AsyncLogSystem::GetInstance().Stop();
      TearDownTest(logDir);
    }
  }

protected:
  std::string logDir;
};

BENCHMARK_DEFINE_F(MultiThreadFixture, BM_MultiThread)
(benchmark::State &state) {
  const size_t kMsgPerThread = 10000;
  for (auto _ : state) {
    for (size_t i = 0; i < kMsgPerThread; ++i) {
      LOG_INFO("t msg {}", i);
    }
  }
  state.SetItemsProcessed(state.iterations() * kMsgPerThread);
}
BENCHMARK_REGISTER_F(MultiThreadFixture, BM_MultiThread)
    ->Arg(10000)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->UseRealTime();

// -------------------------------
// 大消息
// -------------------------------
static void BM_LargeMessage(benchmark::State &state) {
  const size_t kMsgSize = state.range(0);
  const size_t kMessages = state.range(1);
  const std::string logDir = "bench_large";
  SetUpTest(logDir);

  AsyncLogSystem::GetInstance().Init();
  std::string large(kMsgSize, 'X');

  for (auto _ : state) {
    for (size_t i = 0; i < kMessages; ++i)
      LOG_INFO("large {}", large);
  }

  AsyncLogSystem::GetInstance().Stop();

  state.SetBytesProcessed(state.iterations() * kMessages * kMsgSize);
  TearDownTest(logDir);
}
BENCHMARK(BM_LargeMessage)
    ->Args({100, 10000})
    ->Args({1000, 10000})
    ->Args({5000, 10000});

// -------------------------------
// 文件轮转（by size）
// -------------------------------
static void BM_FileRotation(benchmark::State &state) {
  const std::string logDir = "bench_rotation";
  SetUpTest(logDir);

  auto &cfg = Config::GetInstance();
  cfg.rotation.maxFileSize = 1024 * 1024; // 1 MB
  cfg.rotation.maxFileCount = 10;

  AsyncLogSystem::GetInstance().Init();
  std::string payload(1000, 'R');

  for (auto _ : state) {
    for (size_t i = 0; i < 2000; ++i) {
      LOG_INFO("rot {}", payload);
    }
  }

  AsyncLogSystem::GetInstance().Stop();

  TearDownTest(logDir);
}
BENCHMARK(BM_FileRotation);

// -------------------------------
// 内存压力（持续写入）
// -------------------------------
class MemoryPressureFixture : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      PrintMemoryUsage("Before Init");
      logDir = "bench_memory";
      SetUpTest(logDir);
      AsyncLogSystem::GetInstance().Init();
      PrintMemoryUsage("After Init");
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      PrintMemoryUsage("Before Stop");
      AsyncLogSystem::GetInstance().Stop();
      TearDownTest(logDir);
      PrintMemoryUsage("After Stop");
    }
  }

protected:
  std::string logDir;
};

BENCHMARK_DEFINE_F(MemoryPressureFixture, BM_MemoryPressure)
(benchmark::State &state) {
  const size_t kMsgPerThread = 50000;
  const size_t kMsgSize = 500;
  std::string payload(kMsgSize, 'M');

  for (auto _ : state) {
    for (size_t i = 0; i < kMsgPerThread; ++i) {
      LOG_INFO("mem {}", payload);
    }
  }
  state.SetItemsProcessed(state.iterations() * kMsgPerThread);
}
BENCHMARK_REGISTER_F(MemoryPressureFixture, BM_MemoryPressure)
    ->Threads(4) // Keep the original 4 threads
    ->UseRealTime();

// Google Benchmark 主入口
// -------------------------------
BENCHMARK_MAIN();