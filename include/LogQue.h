#pragma once

#include "LogMessage.h"
#include "third/concurrentqueue.h" // ConcurrentQueue

#include <chrono>
#include <cstddef>
#include <thread>

namespace lyf {

enum class QueueFullPolicy {
  BLOCK = 0, // 队列满时阻塞
  DROP = 1   // 队列满时丢弃
};

inline constexpr std::string_view
QueueFullPolicyToString(QueueFullPolicy policy) {
  switch (policy) {
  case QueueFullPolicy::BLOCK:
    return "BLOCK";
  case QueueFullPolicy::DROP:
    return "DROP";
  default:
    return "UNKNOWN";
  }
}

struct QueConfig {
  constexpr static size_t kMaxBlockTimeout_us =
      std::chrono::microseconds::max().count();

  size_t capacity;
  QueueFullPolicy full_policy;
  size_t block_timeout_us; // 最大阻塞时间，超过则丢弃

  QueConfig(size_t capacity = 8192,
            QueueFullPolicy policy = QueueFullPolicy::BLOCK,
            size_t timeout_us = kMaxBlockTimeout_us)
      : capacity(capacity), full_policy(policy), block_timeout_us(timeout_us) {}
};

class LogQueue {
public:
  using ConcurrentQueueType = moodycamel::ConcurrentQueue<LogMessage>;

  LogQueue(const QueConfig &cfg)
      : cfg_(cfg), que_(cfg.capacity == 0 ? 8192 : cfg.capacity),
        capacity_(cfg.capacity) {}

  bool Push(LogMessage &&msg, bool force = false) {
    if (!force && cfg_.capacity > 0 && que_.size_approx() >= capacity_) {
      // 需要背压处理
      return HandleBackPressure(std::move(msg));
    }
    return que_.enqueue(std::move(msg));
  }

  size_t PopBatch(std::vector<LogMessage> &output, size_t batch_size = 1024) {
    return que_.try_dequeue_bulk(std::back_inserter(output), batch_size);
  }

  size_t size_approx() const { return que_.size_approx(); }

private:
  bool HandleBackPressure(LogMessage &&msg) {
    if (cfg_.full_policy == QueueFullPolicy::DROP) {
      // 返回 false，msg 析构，buffer 自动归还 Pool
      return false;
    }

    // BLOCK 策略：循环等待直到 size_approx 降下来
    size_t sleep_us = 1;
    auto start = std::chrono::steady_clock::now();

    while (true) {
      if (que_.size_approx() < capacity_) {
        return que_.enqueue(std::move(msg));
      }

      // 超时检查
      if (std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start)
              .count() > cfg_.block_timeout_us) {
        return false; // 超时丢弃
      }

      std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
      if (sleep_us < 1024) {
        sleep_us *= 2;
      }
    }
  }

private:
  QueConfig cfg_;
  ConcurrentQueueType que_;
  size_t capacity_;
};

} // namespace lyf
