#pragma once
#include "LogConfig.h"
#include "LogQue.h"
#include "Sink.h"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace lyf {
class AsyncLogger {
  using system_clock = std::chrono::system_clock;

public:
  AsyncLogger(const LogConfig &config)
      : config_(config), queue_(config.GetQueueConfig()), running_(true) {
    UpdateNow();
    worker_thread_ = std::thread(&AsyncLogger::WorkerLoop, this);
    timer_thread_ = std::thread(&AsyncLogger::TimerLoop, this);
  }

  ~AsyncLogger() {
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }
    // Sinks 会在析构时自动 Flush
  }

  void AddSink(std::shared_ptr<ILogSink> sink) { sinks_.push_back(sink); }
  bool Commit(LogMessage &&msg) { return queue_.Push(std::move(msg)); }
  void Flush() {
    for (auto &sink : sinks_) {
      sink->Flush();
    }
  }
  void Sync() {
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();
    // 构造 FLUSH 消息推入队列
    if (queue_.Push(LogMessage(&prom), true)) {
      // 等待 Worker 处理到这条消息
      fut.wait();
    }
  }

  size_t GetDropCount() const { return drop_cnt_; }
  void AddDropCount(size_t cnt) { drop_cnt_.fetch_add(cnt); }

  int64_t GetCoarseTime() {
    return coarse_time_ns_.load(std::memory_order_relaxed);
  }

private:
  void UpdateNow() {
    coarse_time_ns_.store(system_clock::now().time_since_epoch().count(),
                          std::memory_order_relaxed);
  }

  void TimerLoop() {
    while (running_) {
      UpdateNow();
      std::this_thread::sleep_for(
          std::chrono::milliseconds(LogConfig::kCoarseTimeIntervalMs));
    }
  }

  void WorkerLoop() {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif

    std::vector<LogMessage> buffer_batch;
    size_t batch_size = config_.GetWorkerBatchSize();
    if (batch_size == 0) {
      batch_size = LogConfig::kDefaultWorkerBatchSize;
    }
    buffer_batch.reserve(batch_size);

    while (running_ || queue_.size_approx() > 0) {
      size_t count = queue_.PopBatch(buffer_batch, batch_size);
      if (count > 0) {
        for (const auto &msg : buffer_batch) {
          if (msg.level == LogLevel::FLUSH) {
            // 遇到 Flush 指令，先强制刷新所有 Sink
            for (auto &sink : sinks_) {
              sink->Flush();
            }
            // 通知主线程 Flush 指令之前的数据都处理完了
            if (msg.sync_promise) {
              msg.sync_promise->set_value();
            }
            continue;
          }
          for (auto &sink : sinks_) {
            sink->Log(msg);
          }
        }
        // LogMessage 在 vector clear 时析构，Buffer 自动归还 Pool
        buffer_batch.clear();
      } else {
        if (running_) {
          std::this_thread::sleep_for(
              std::chrono::microseconds(LogConfig::kDefaultWorkerIdleSleepUs));
        } else {
          break; // running=false 且队列空，彻底退出
        }
      }
    }
  }

private:
  const LogConfig &config_;
  LogQueue queue_;
  std::atomic<bool> running_;
  std::thread worker_thread_;
  std::atomic<size_t> drop_cnt_{0};
  std::vector<std::shared_ptr<ILogSink>> sinks_;

  // 时间更新线程
  std::thread timer_thread_;
  std::atomic<int64_t> coarse_time_ns_{0}; // 粗粒度时间戳
};

} // namespace lyf
