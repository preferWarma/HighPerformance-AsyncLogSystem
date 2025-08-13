// log_benchmark.cpp
#include "Config.h"
#include "LogSystem.h"

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

class LogBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State &state) override {
    std::filesystem::create_directory(log_dir);
  }

  void TearDown(const ::benchmark::State &state) override {
    std::filesystem::remove_all(log_dir);
  }

protected:
  const std::string log_dir = "./bench_log_system";
};

class MyLoggerFixture : public LogBenchmark {
public:
  void SetUp(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      LogBenchmark::SetUp(state);
      PrintMemoryUsage("Before Init");
      auto &cfg = lyf::Config::GetInstance();
      cfg.output.toConsole = false;
      cfg.output.toFile = true;
      cfg.output.logRootDir = log_dir;
      cfg.cloud.enable = false;
      cfg.basic.maxQueueSize = 81920; // 限制队列长度减少内存占用
      cfg.basic.queueFullPolicy = lyf::QueueFullPolicy::BLOCK; // 满则阻塞
      lyf::AsyncLogSystem::GetInstance().Init();
      PrintMemoryUsage("After Init");
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (state.thread_index() == 0) {
      PrintMemoryUsage("Before Stop");
      lyf::AsyncLogSystem::GetInstance().Stop();
      LogBenchmark::TearDown(state);
      PrintMemoryUsage("After Stop");
    }
  }
};

// ===================================================================
// 1. 端到端吞吐量测试
// ===================================================================
BENCHMARK_F(MyLoggerFixture, Throughput)(benchmark::State &state) {
  constexpr int messages_per_iteration = 10000;
  for (auto _ : state) {
    for (int i = 0; i < messages_per_iteration; ++i) {
      LOG_INFO("Thread {} message {}", state.thread_index(), i);
    }
    lyf::AsyncLogSystem::GetInstance().Flush();
  }
  state.SetItemsProcessed(state.iterations() * messages_per_iteration);
}
BENCHMARK_REGISTER_F(MyLoggerFixture, Throughput)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8);

// ===================================================================
// 2.1 队列入队延时测试(队列有上限, 阻塞策略)
// ===================================================================
BENCHMARK_F(MyLoggerFixture, Latency)(benchmark::State &state) {
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Latency test message {}", state.iterations());
    auto end = std::chrono::high_resolution_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - start).count());
  }
}
BENCHMARK_REGISTER_F(MyLoggerFixture, Latency)->UseManualTime();

// ===================================================================
// 2.2 队列入队延时测试(队列无上限)
// ===================================================================
BENCHMARK_F(MyLoggerFixture, Latency_EnoughMemory)(benchmark::State &state) {
  lyf::Config::GetInstance().basic.maxQueueSize = 0; // 无上限
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    LOG_INFO("Latency test message {}", state.iterations());
    auto end = std::chrono::high_resolution_clock::now();
    state.SetIterationTime(std::chrono::duration<double>(end - start).count());
  }
}
BENCHMARK_REGISTER_F(MyLoggerFixture, Latency_EnoughMemory)->UseManualTime();

// ===================================================================
// 3. 消息体大小测试 (不同消息体大小)
// ===================================================================
BENCHMARK_DEFINE_F(MyLoggerFixture, Payload)(benchmark::State &state) {
  const size_t messageCount = 1000;
  const size_t messageSize = state.range(0);
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

// ===================================================================
// 4. 文件轮转效率测试
// ===================================================================
class RotationFixture : public LogBenchmark {
public:
  void SetUp(const ::benchmark::State &state) override {
    LogBenchmark::SetUp(state);
    auto &cfg = lyf::Config::GetInstance();
    cfg.output.toConsole = false;
    cfg.output.toFile = true;
    cfg.output.logRootDir = log_dir;
    cfg.cloud.enable = false;
    cfg.rotation.maxFileSize = 10 * 1024 * 1024; // 10 MB
    cfg.rotation.maxFileCount = 20;
    lyf::AsyncLogSystem::GetInstance().Init();
  }

  void TearDown(const ::benchmark::State &state) override {
    lyf::AsyncLogSystem::GetInstance().Stop();
    LogBenchmark::TearDown(state);
  }
};

BENCHMARK_F(RotationFixture, Rotation)(benchmark::State &state) {
  std::string payload(1000, 'R');
  for (auto _ : state) {
    for (size_t i = 0; i < 2000; ++i) {
      LOG_INFO("rot {}", payload);
    }
  }
}

// ===================================================================
// 5. 内存压力测试 (生产者 > 消费者, 无Flush)
// ===================================================================
BENCHMARK_F(MyLoggerFixture, MemoryPressure)(benchmark::State &state) {
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
BENCHMARK_REGISTER_F(MyLoggerFixture, MemoryPressure)
    ->Threads(4)
    ->UseRealTime();

// ===================================================================
BENCHMARK_MAIN();