#pragma once
#include "LogQue.h"
#include "Sink.h"

namespace lyf {
class AsyncLogger {
public:
  AsyncLogger(const QueConfig &config) : queue_(config), running_(true) {
    worker_thread_ = std::thread(&AsyncLogger::WorkerLoop, this);
  }

  ~AsyncLogger() {
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
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

private:
  void WorkerLoop() {
    std::vector<LogMessage> buffer_batch;
    buffer_batch.reserve(128); // 预分配

    while (running_ || queue_.size_approx() > 0) {
      size_t count = queue_.PopBatch(buffer_batch, 128);
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
          std::this_thread::sleep_for(std::chrono::microseconds(500));
        } else {
          break; // running=false 且队列空，彻底退出
        }
      }
    }
  }

private:
  LogQueue queue_;
  std::atomic<bool> running_;
  std::thread worker_thread_;
  std::atomic<size_t> drop_cnt_{0};
  std::vector<std::shared_ptr<ILogSink>> sinks_;
};

} // namespace lyf