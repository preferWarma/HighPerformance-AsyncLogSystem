#pragma once

#include "Config.h"
#include "third/concurrentqueue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace lyf {
using time_point = std::chrono::system_clock::time_point;
using std::string, std::vector, std::atomic;
using std::chrono::system_clock;

enum class LogLevel {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
  FATAL = 4,
};
inline std::string LevelToString(LogLevel level) {
  switch (level) {
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARN:
    return "WARN";
  case LogLevel::ERROR:
    return "ERROR";
  case LogLevel::FATAL:
    return "FATAL";
  default:
    return "UNKNOWN";
  }
}

struct LogMessage {
  // 日志元数据
  time_point time;
  LogLevel level;
  const char *file_name;
  size_t file_line;
  size_t hash_tid;
  // 日志内容
  string content;

  LogMessage(LogLevel level, const char *file_name, size_t file_line,
             size_t hash_tid, string content)
      : time(system_clock::now()), level(level), file_name(file_name),
        file_line(file_line), hash_tid(hash_tid), content(std::move(content)) {}

  LogMessage(LogMessage &&other) noexcept = default;
  LogMessage &operator=(LogMessage &&other) noexcept = default;
  LogMessage(const LogMessage &other) = default;
  LogMessage &operator=(const LogMessage &other) = default;
};

class LogQueue {
public:
  using ConcurrentQueueType = moodycamel::ConcurrentQueue<LogMessage>;
  using OutputQueType = std::deque<LogMessage>;

  LogQueue(const lyf::Config &config)
      : cfg_(config), queue_(config.basic.maxQueueSize), dropCount_(0),
        currentMaxQueueSize_(cfg_.basic.maxQueueSize) {}

  void Push(LogMessage &&msg) noexcept { PushImpl(std::move(msg)); }
  void Push(const LogMessage &msg) noexcept { PushImpl(msg); }

  size_t PopBatch(OutputQueType &output, size_t batch_size = 1024) {
    return queue_.try_dequeue_bulk(std::back_inserter(output), batch_size);
  }

  size_t size_approx() const { return queue_.size_approx(); }

private:
  template <typename T> void PushImpl(T &&msg) noexcept {
    // 不设限或未达上限
    if (cfg_.basic.maxQueueSize == 0 ||
        queue_.size_approx() <
            currentMaxQueueSize_.load(std::memory_order_relaxed)) {
      queue_.enqueue(std::forward<T>(msg));
    } else { // 需要背压：阻塞策略或丢弃策略
      if (cfg_.basic.queueFullPolicy == QueueFullPolicy::BLOCK) {
        size_t sleepTime_us = 1;
        size_t total_sleepTime_us = 0;
        while (!queue_.try_enqueue(std::forward<T>(msg))) {
          std::this_thread::sleep_for(std::chrono::microseconds(sleepTime_us));
          total_sleepTime_us += sleepTime_us;
          sleepTime_us = std::min(sleepTime_us * 2, cfg_.basic.maxBlockTime_us);
          if (total_sleepTime_us > cfg_.basic.maxBlockTime_us &&
              currentMaxQueueSize_.load(std::memory_order_relaxed) <
                  cfg_.basic.maxQueueSize * cfg_.basic.autoExpandMultiply) {
            // 超过可容忍的最大阻塞时间且未到硬性上限，允许扩容
            queue_.enqueue(std::forward<T>(msg));
            currentMaxQueueSize_.fetch_add(1, std::memory_order_relaxed);
            return;
          }
        }
      } else if (cfg_.basic.queueFullPolicy == QueueFullPolicy::DROP) {
        dropCount_++;
        if (dropCount_ > cfg_.basic.maxDropCount &&
            currentMaxQueueSize_.load(std::memory_order_relaxed) <
                cfg_.basic.maxQueueSize * cfg_.basic.autoExpandMultiply) {
          // 超过可容忍的最大丢弃次数且未到硬性上限，允许扩容
          queue_.enqueue(std::forward<T>(msg));
          currentMaxQueueSize_.fetch_add(1, std::memory_order_relaxed);
          dropCount_.store(0, std::memory_order_relaxed);
          return;
        }
      }
    }
  }

private:
  const lyf::Config &cfg_;
  ConcurrentQueueType queue_;
  std::atomic<size_t> dropCount_;
  std::atomic<size_t> currentMaxQueueSize_;
}; // class LogQueue

} // namespace lyf
