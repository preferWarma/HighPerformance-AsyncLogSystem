#pragma once

#include "Config.h"
#include "enum.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace lyf {
using time_point = std::chrono::system_clock::time_point;
using std::mutex, std::condition_variable, std::lock_guard;
using std::string, std::queue, std::vector, std::atomic;
using std::chrono::system_clock;

struct LogMessage {
  LogLevel level;  // 日志级别
  string content;  // 日志内容
  time_point time; // 日志时间

  LogMessage(LogLevel level, string content)
      : level(level), content(std::move(content)), time(system_clock::now()) {}
};

// 日志队列
class LogQueue {
  using milliseconds = std::chrono::milliseconds;

public:
  using QueueType = queue<LogMessage>;

public:
  LogQueue(size_t maxSize) : isStop_(false), maxSize_(maxSize) {}

  // 生产端：添加日志消息
  void Push(LogMessage &&msg) {
    lock_guard<mutex> lock(queMtx_);
    currentQueue_.push(std::move(msg));
    if (currentQueue_.size() >= maxSize_) {
      fullQueues_.push(std::move(currentQueue_));
      currentQueue_ = QueueType();
      notEmpty_.notify_one();
    }
  }

  // 消费端：获取所有日志消息（添加超时机制）
  bool PopAll(QueueType &output, milliseconds timeout = milliseconds(100)) {
    std::unique_lock<mutex> lock(queMtx_);
    notEmpty_.wait_for(lock, timeout,
                       [this] { return !fullQueues_.empty() || isStop_; });

    if (fullQueues_.empty() && currentQueue_.empty()) {
      return false; // Nothing to do
    }

    queue<QueueType> localFullQueues;
    if (!fullQueues_.empty()) {
      localFullQueues.swap(fullQueues_);
    }

    if (!currentQueue_.empty()) {
      localFullQueues.push(std::move(currentQueue_));
      currentQueue_ = QueueType();
    }
    lock.unlock();

    if (localFullQueues.empty()) {
      return false;
    }

    // Merge all local queues into the output queue
    output = std::move(localFullQueues.front());
    localFullQueues.pop();
    while (!localFullQueues.empty()) {
      auto &q = localFullQueues.front();
      while (!q.empty()) {
        output.push(std::move(q.front()));
        q.pop();
      }
      localFullQueues.pop();
    }
    return true;
  }

  void Stop() {
    if (isStop_.exchange(true)) {
      notEmpty_.notify_all();
      return;
    }
  }

private:
  QueueType currentQueue_;      // 当前生产队列
  queue<QueueType> fullQueues_; // 满了的日志队列
  mutable mutex queMtx_;        // 互斥锁
  condition_variable notEmpty_; // 条件变量, 用于等待队列有数据
  atomic<bool> isStop_;         // 是否关闭
  size_t maxSize_;              // 最大队列大小
}; // class LogQueue

} // namespace lyf
