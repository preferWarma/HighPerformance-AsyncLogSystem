#include "Logger.h"
#include "Sink.h"
#include <algorithm>
#include <benchmark/benchmark.h>
#include <chrono>
#include <numeric>
#include <string>
#include <vector>

namespace {
class NullSink : public lyf::ILogSink {
public:
  void Log(const lyf::LogMessage &msg) override {
    buffer_.clear();
    formatter_.Format(msg, buffer_);
  }
  void Flush() override {}
};

std::once_flag g_init_flag;

void InitLoggerOnce() {
  std::call_once(g_init_flag, [] {
    lyf::LogConfig cfg;
    cfg.SetQueueCapacity(65536)
        .SetQueueFullPolicy(lyf::QueueFullPolicy::BLOCK)
        .SetQueueBlockTimeoutUs(lyf::QueConfig::kMaxBlockTimeout_us)
        .SetBufferPoolSize(65536)
        .SetLevel(lyf::LogLevel::DEBUG)
        .SetLogPath("");
    lyf::Logger::Instance().Init(cfg);
    lyf::Logger::Instance().AddSink(std::make_shared<NullSink>());
  });
}

uint64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

uint64_t Percentile(std::vector<uint64_t> &values, double p) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  auto idx = static_cast<size_t>((values.size() - 1) * p);
  return values[idx];
}

void RecordStats(benchmark::State &state, std::vector<uint64_t> &samples,
                 size_t msg_size) {
  if (samples.empty()) {
    return;
  }
  uint64_t sum = std::accumulate(samples.begin(), samples.end(), uint64_t{0});
  double avg = static_cast<double>(sum) / samples.size();
  double total_sec = static_cast<double>(sum) / 1e9;
  double logs_per_sec =
      total_sec > 0 ? static_cast<double>(samples.size()) / total_sec : 0.0;
  double mb_per_sec =
      logs_per_sec * static_cast<double>(msg_size) / (1024.0 * 1024.0);
  uint64_t p50 = Percentile(samples, 0.50);
  uint64_t p90 = Percentile(samples, 0.90);
  uint64_t p99 = Percentile(samples, 0.99);
  state.counters["avg_ns"] = avg;
  state.counters["p50_ns"] = static_cast<double>(p50);
  state.counters["p90_ns"] = static_cast<double>(p90);
  state.counters["p99_ns"] = static_cast<double>(p99);
  state.counters["logs_per_sec"] = logs_per_sec;
  state.counters["mb_per_sec"] = mb_per_sec;
}

void LogByLevel(lyf::LogLevel level, const std::string &payload) {
  switch (level) {
  case lyf::LogLevel::DEBUG:
    DEBUG("{}", payload);
    break;
  case lyf::LogLevel::INFO:
    INFO("{}", payload);
    break;
  case lyf::LogLevel::WARN:
    WARN("{}", payload);
    break;
  case lyf::LogLevel::ERROR:
    ERROR("{}", payload);
    break;
  default:
    INFO("{}", payload);
    break;
  }
}

void RunLogBenchmark(benchmark::State &state, lyf::LogLevel level,
                     size_t msg_size, size_t sync_every) {
  InitLoggerOnce();
  lyf::Logger::Instance().SetLevel(lyf::LogLevel::DEBUG);
  std::string payload(msg_size, 'x');
  std::vector<uint64_t> samples;
  samples.reserve(static_cast<size_t>(state.iterations()));
  size_t counter = 0;

  for (auto _ : state) {
    uint64_t begin = NowNs();
    LogByLevel(level, payload);
    uint64_t end = NowNs();
    samples.push_back(end - begin);
    if (sync_every > 0) {
      counter++;
      if (counter % sync_every == 0) {
        lyf::Logger::Instance().Sync();
      }
    }
  }

  lyf::Logger::Instance().Sync();
  RecordStats(state, samples, msg_size);
}
} // namespace

static void BM_LogLevel_Async_DEBUG(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::DEBUG, 64, 0);
}
static void BM_LogLevel_Async_INFO(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::INFO, 64, 0);
}
static void BM_LogLevel_Async_WARN(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::WARN, 64, 0);
}
static void BM_LogLevel_Async_ERROR(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::ERROR, 64, 0);
}

static void BM_MessageSize_Async(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::INFO,
                  static_cast<size_t>(state.range(0)), 0);
}

static void BM_Sync_Compare_Async(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::INFO, 128, 0);
}
static void BM_Sync_Compare_Sync(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::INFO, 128, 1);
}

static void BM_MultiThread_Async(benchmark::State &state) {
  RunLogBenchmark(state, lyf::LogLevel::INFO, 128, 0);
}

BENCHMARK(BM_LogLevel_Async_DEBUG)->MinTime(1.0);
BENCHMARK(BM_LogLevel_Async_INFO)->MinTime(1.0);
BENCHMARK(BM_LogLevel_Async_WARN)->MinTime(1.0);
BENCHMARK(BM_LogLevel_Async_ERROR)->MinTime(1.0);

BENCHMARK(BM_MessageSize_Async)
    ->Arg(16)
    ->Arg(128)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(2048)
    ->Arg(3500)
    ->MinTime(1.0);

BENCHMARK(BM_Sync_Compare_Async)->MinTime(1.0);
BENCHMARK(BM_Sync_Compare_Sync)->MinTime(1.0);

BENCHMARK(BM_MultiThread_Async)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime()
    ->MinTime(1.0);

BENCHMARK_MAIN();
