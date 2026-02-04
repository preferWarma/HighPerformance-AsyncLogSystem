#pragma once
#include "LogQue.h"
#include "Sink.h"
#include "tool/Singleton.h"
#include <atomic>
#include <cstddef>
#include <format>
#include <memory>
#include <thread>
#include <vector>

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

namespace lyf {

// 全局单例管理器
class Logger : public Singleton<Logger> {
  friend class Singleton<Logger>;

public:
  void Init(const QueConfig &cfg) {
    BufferPool::Instance().Init(1000);
    impl_ = std::make_unique<AsyncLogger>(cfg);
  }

  void AddSink(std::shared_ptr<ILogSink> sink) {
    if (impl_) {
      impl_->AddSink(sink);
    }
  }

  AsyncLogger *GetImpl() { return impl_.get(); }
  LogLevel GetLevel() const { return level_; }
  void SetLevel(LogLevel lv) { level_ = lv; }

private:
  std::unique_ptr<AsyncLogger> impl_;
  std::atomic<LogLevel> level_{LogLevel::INFO};
  std::atomic<size_t> drop_cnt_{0};
};

template <typename... Args>
void LogSubmit(LogLevel level, const char *file, int line,
               std::format_string<Args...> fmt, Args &&...args) {
  // 从池拿 Buffer
  auto *buf = BufferPool::Instance().Alloc();
  try {
    // 格式化
    auto result = std::format_to_n(buf->data, LogBuffer::SIZE - 1, fmt,
                                   std::forward<Args>(args)...);
    buf->length = result.size;
    buf->data[buf->length] = '\0';
  } catch (...) {
    buf->reset();
  }
  LogMessage msg(level, file, line, std::this_thread::get_id(), buf);

  if (auto *logger = Logger::Instance().GetImpl()) {
    if (!logger->Commit(std::move(msg))) {
      logger->AddDropCount(1);
    }
  }
}

} // namespace lyf

// --- 用户宏定义 ---
inline static std::atomic<size_t> dropCount{0};

// 编译期/运行期 级别过滤
#define LOG_BASE(level, ...)                                                   \
  do {                                                                         \
    if (lyf::Logger::Instance().GetLevel() <= level) {                         \
      lyf::LogSubmit(level, __FILE__, __LINE__, __VA_ARGS__);                  \
    }                                                                          \
  } while (0)

#define LOG_DEBUG(...) LOG_BASE(lyf::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...) LOG_BASE(lyf::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(...) LOG_BASE(lyf::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(...) LOG_BASE(lyf::LogLevel::ERROR, __VA_ARGS__)
#define LOG_FATAL(...) LOG_BASE(lyf::LogLevel::FATAL, __VA_ARGS__)