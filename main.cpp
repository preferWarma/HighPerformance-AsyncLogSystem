#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
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

/*
可配置参数（命令行）
--logs N ：总日志条数（默认 1,000,000）
--warmup-logs N ：预热条数（默认 0）
--threads N ：线程数（默认硬件并发）
--capacity N ：队列容量（默认 65536）
--policy BLOCK|DROP ：队列满策略（默认 BLOCK）
--timeout-us N ：BLOCK 超时（默认无限）
--buffer-pool N ：BufferPool 初始数量（默认 65536）
--sink file|console ：输出 sink（默认 file）
--log-file path ：日志文件路径（默认 app.log）
--level DEBUG|INFO|... ：Logger 级别（默认 INFO）
--sample-rate N ：每 N 条采 1 个延迟样本（默认 1000，0 表示不采样）
--count-lines ：额外统计文件行数（可选）

// 示例命令
./main --threads 4 --logs 10000000 --warmup-logs 1000 --policy BLOCK --capacity
65536 --sink file --buffer-pool 65536 --log-file app.log --sample-rate 1000
*/

struct BenchmarkConfig {
  size_t log_count = 1'000'000;
  size_t warmup_logs = 0;
  size_t thread_count = std::max(1u, std::thread::hardware_concurrency());
  size_t capacity = 65536;
  QueueFullPolicy policy = QueueFullPolicy::BLOCK;
  size_t timeout_us = QueConfig::kMaxBlockTimeout_us;
  size_t buffer_pool_size = 65536;
  LogLevel level = LogLevel::INFO;
  std::string sink = "file";
  std::string log_file = "app.log";
  size_t sample_rate = 1000;
  bool count_lines = false;
};

struct ThreadStats {
  uint64_t count = 0;
  uint64_t sum_ns = 0;
  uint64_t min_ns = std::numeric_limits<uint64_t>::max();
  uint64_t max_ns = 0;
  std::vector<uint64_t> samples;
};

struct AggregateStats {
  uint64_t count = 0;
  uint64_t sum_ns = 0;
  uint64_t min_ns = std::numeric_limits<uint64_t>::max();
  uint64_t max_ns = 0;
  std::vector<uint64_t> samples;
};

static uint64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

static QueueFullPolicy ParsePolicy(const std::string &value) {
  if (value == "DROP") {
    return QueueFullPolicy::DROP;
  }
  return QueueFullPolicy::BLOCK;
}

static LogLevel ParseLevel(const std::string &value) {
  if (value == "DEBUG")
    return LogLevel::DEBUG;
  if (value == "INFO")
    return LogLevel::INFO;
  if (value == "WARN")
    return LogLevel::WARN;
  if (value == "ERROR")
    return LogLevel::ERROR;
  if (value == "FATAL")
    return LogLevel::FATAL;
  return LogLevel::INFO;
}

static void truncateLogFile(const std::string &logfile) {
  std::fstream ofs(logfile, std::ios::out | std::ios::trunc);
  ofs.close();
}

static size_t CountLines(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return 0;
  }

  // 统计换行符数量
  return std::count(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>(), '\n');
}

static BenchmarkConfig ParseArgs(int argc, char **argv) {
  BenchmarkConfig cfg;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](size_t &out) {
      if (i + 1 < argc) {
        out = static_cast<size_t>(std::stoull(argv[++i]));
      }
    };
    if (arg == "--logs") {
      next(cfg.log_count);
    } else if (arg == "--warmup-logs") {
      next(cfg.warmup_logs);
    } else if (arg == "--threads") {
      next(cfg.thread_count);
    } else if (arg == "--capacity") {
      next(cfg.capacity);
    } else if (arg == "--timeout-us") {
      next(cfg.timeout_us);
    } else if (arg == "--buffer-pool") {
      next(cfg.buffer_pool_size);
    } else if (arg == "--sample-rate") {
      next(cfg.sample_rate);
    } else if (arg == "--sink" && i + 1 < argc) {
      cfg.sink = argv[++i];
    } else if (arg == "--log-file" && i + 1 < argc) {
      cfg.log_file = argv[++i];
    } else if (arg == "--policy" && i + 1 < argc) {
      cfg.policy = ParsePolicy(argv[++i]);
    } else if (arg == "--level" && i + 1 < argc) {
      cfg.level = ParseLevel(argv[++i]);
    } else if (arg == "--count-lines") {
      cfg.count_lines = true;
    }
  }
  if (cfg.thread_count == 0) {
    cfg.thread_count = 1;
  }
  return cfg;
}

static void InitLogger(const BenchmarkConfig &cfg) {
  QueConfig que_cfg(cfg.capacity, cfg.policy, cfg.timeout_us);
  auto &logger = Logger::Instance();
  logger.Init(que_cfg, cfg.buffer_pool_size);
  logger.SetLevel(cfg.level);
  if (cfg.sink == "console") {
    logger.AddSink(std::make_shared<ConsoleSink>());
  } else {
    logger.AddSink(std::make_shared<FileSink>(cfg.log_file));
  }
}

static AggregateStats Aggregate(const std::vector<ThreadStats> &stats) {
  AggregateStats agg;
  size_t total_samples = 0;
  for (const auto &s : stats) {
    agg.count += s.count;
    agg.sum_ns += s.sum_ns;
    agg.min_ns = std::min(agg.min_ns, s.min_ns);
    agg.max_ns = std::max(agg.max_ns, s.max_ns);
    total_samples += s.samples.size();
  }
  agg.samples.reserve(total_samples);
  for (const auto &s : stats) {
    agg.samples.insert(agg.samples.end(), s.samples.begin(), s.samples.end());
  }
  return agg;
}

static uint64_t Percentile(std::vector<uint64_t> &values, double p) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  double idx = (values.size() - 1) * p;
  size_t i = static_cast<size_t>(idx);
  return values[i];
}

int main(int argc, char **argv) {
  auto cfg = ParseArgs(argc, argv);
  InitLogger(cfg);
  if (cfg.sink != "console") {
    truncateLogFile(cfg.log_file);
  }

  std::vector<std::thread> threads;
  threads.reserve(cfg.thread_count);
  std::vector<ThreadStats> thread_stats(cfg.thread_count);
  std::atomic<bool> start_flag{false};

  size_t base = cfg.log_count / cfg.thread_count;
  size_t remain = cfg.log_count % cfg.thread_count;
  size_t warm_base = cfg.warmup_logs / cfg.thread_count;
  size_t warm_remain = cfg.warmup_logs % cfg.thread_count;

  for (size_t t = 0; t < cfg.thread_count; ++t) {
    size_t count = base + (t < remain ? 1 : 0);
    size_t warm_count = warm_base + (t < warm_remain ? 1 : 0);
    threads.emplace_back([&, t, count, warm_count]() {
      for (size_t i = 0; i < warm_count; ++i) {
        INFO("warmup {}", i);
      }
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      auto &stats = thread_stats[t];
      if (cfg.sample_rate > 0) {
        stats.samples.reserve(count / cfg.sample_rate + 1);
      }
      for (size_t i = 0; i < count; ++i) {
        uint64_t begin_ns = NowNs();
        INFO("Hello, LogSystem! {} {}", t, i);
        uint64_t end_ns = NowNs();
        uint64_t lat = end_ns - begin_ns;
        stats.count += 1;
        stats.sum_ns += lat;
        stats.min_ns = std::min(stats.min_ns, lat);
        stats.max_ns = std::max(stats.max_ns, lat);
        if (cfg.sample_rate > 0 && (i % cfg.sample_rate == 0)) {
          stats.samples.push_back(lat);
        }
      }
    });
  }

  PrintMemoryUsage("Before benchmark");

  uint64_t start_ns = NowNs();
  start_flag.store(true, std::memory_order_release);
  for (auto &th : threads) {
    th.join();
  }
  uint64_t submit_end_ns = NowNs();
  PrintMemoryUsage("After submit");

  Logger::Instance().Sync();
  uint64_t end_ns = NowNs();
  PrintMemoryUsage("After sync");

  auto agg = Aggregate(thread_stats);
  uint64_t total_time_ns = end_ns - start_ns;
  uint64_t submit_time_ns = submit_end_ns - start_ns;
  uint64_t sync_time_ns = end_ns - submit_end_ns;
  double avg_ns =
      agg.count > 0 ? static_cast<double>(agg.sum_ns) / agg.count : 0;

  uint64_t p50 = Percentile(agg.samples, 0.50);
  uint64_t p95 = Percentile(agg.samples, 0.95);
  uint64_t p99 = Percentile(agg.samples, 0.99);
  uint64_t p999 = Percentile(agg.samples, 0.999);

  uint64_t min_lat_ns = agg.min_ns;
  uint64_t max_lat_ns = agg.max_ns;

  uint64_t drop_count = Logger::Instance().GetDropCount();
  uint64_t logfile_size_MB = 0;
  if (cfg.sink != "console") {
    logfile_size_MB = std::filesystem::file_size(cfg.log_file) / (1024 * 1024);
  }

  std::cout << "threads: " << cfg.thread_count << std::endl;
  std::cout << "logs: " << cfg.log_count << std::endl;
  std::cout << "policy: " << QueueFullPolicyToString(cfg.policy) << std::endl;
  std::cout << "capacity: " << cfg.capacity << std::endl;
  std::cout << "buffer pool size: " << cfg.buffer_pool_size << std::endl;
  std::cout << "total time: " << total_time_ns / 1e9 << " s" << std::endl;
  std::cout << "submit time: " << submit_time_ns / 1e9 << " s" << std::endl;
  std::cout << "sync time: " << sync_time_ns / 1e9 << " s" << std::endl;
  std::cout << "avg submit latency: " << avg_ns << " ns" << std::endl;
  std::cout << "min/max latency: " << min_lat_ns << "/" << max_lat_ns << " ns"
            << std::endl;
  std::cout << "p50/p95/p99/p999: " << p50 << "/" << p95 << "/" << p99 << "/"
            << p999 << " ns" << std::endl;
  std::cout << "drop count: " << drop_count << std::endl;
  if (cfg.sink != "console") {
    std::cout << "logfile: " << cfg.log_file << std::endl;
    std::cout << "logfile size: " << logfile_size_MB << " MB" << std::endl;
    double throughput =
        total_time_ns > 0 ? 1.0 * logfile_size_MB / (total_time_ns / 1e9) : 0.0;
    std::cout << "avg throughput: " << throughput << " MB/s" << std::endl;
  }

  if (cfg.count_lines && cfg.sink != "console") {
    auto line_count = CountLines(cfg.log_file);
    std::cout << "line count: " << line_count << std::endl;
  }

  return 0;
}
