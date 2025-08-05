#pragma once

#include "Config.h"
#include "third/concurrentqueue.h"
#include "Enum.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace lyf {
using time_point = std::chrono::system_clock::time_point;
using std::string, std::vector, std::atomic;
using std::chrono::system_clock;

struct LogMessage {
  LogLevel level;
  string content;
  time_point time;

  LogMessage(LogLevel level, string content)
      : level(level), content(std::move(content)), time(system_clock::now()) {}

  // moodycamel::ConcurrentQueue 需要元素是可移动的
  LogMessage(LogMessage &&other) noexcept = default;
  LogMessage &operator=(LogMessage &&other) noexcept = default;

  // 并且可复制，用于控制台队列的复制
  LogMessage(const LogMessage &other) = default;
  LogMessage &operator=(const LogMessage &other) = default;
};

// 日志队列
class LogQueue {
  using milliseconds = std::chrono::milliseconds;
  using ConcurrentQueue = moodycamel::ConcurrentQueue<LogMessage>;

public:
  // LogSystem 中用于批量处理的队列类型
  using QueueType = std::deque<LogMessage>;

public:
  LogQueue(size_t maxSize) : queue_(maxSize) {}

  // 生产端：添加日志消息
  void Push(LogMessage &&msg) noexcept { queue_.enqueue(std::move(msg)); }

  void Push(const LogMessage &msg) noexcept { queue_.enqueue(msg); }

  // 消费端：获取所有日志消息(批量获取,一次获取最多batch_size条消息)
  size_t PopAll(QueueType &output, size_t batch_size = 1024) {
    return queue_.try_dequeue_bulk(std::back_inserter(output), batch_size);
  }

  // 改为无锁队列后 Stop 不需要做任何事
  void Stop() {}

  size_t size_approx() const { return queue_.size_approx(); }

private:
  ConcurrentQueue queue_;
}; // class LogQueue

} // namespace lyf
