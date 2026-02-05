#pragma once

#include "Logger_impl.h"
#include "tool/Singleton.h"
#include <atomic>
#include <cstddef>
#include <format>
#include <memory>
#include <thread>

#define LOG_BASE(level, ...)                                                   \
  do {                                                                         \
    if (lyf::Logger::Instance().GetLevel() <= level) {                         \
      lyf::LogSubmit(level, __FILE__, __LINE__, __VA_ARGS__);                  \
    }                                                                          \
  } while (0)

// --- 用户宏定义 ---
#define DEBUG(...) LOG_BASE(lyf::LogLevel::DEBUG, __VA_ARGS__)
#define INFO(...) LOG_BASE(lyf::LogLevel::INFO, __VA_ARGS__)
#define WARN(...) LOG_BASE(lyf::LogLevel::WARN, __VA_ARGS__)
#define ERROR(...) LOG_BASE(lyf::LogLevel::ERROR, __VA_ARGS__)
#define FATAL(...) LOG_BASE(lyf::LogLevel::FATAL, __VA_ARGS__)

namespace lyf {

// 全局单例管理器
class Logger : public Singleton<Logger> {
  friend class Singleton<Logger>;

public:
  void Init(const QueConfig &cfg, size_t buf_pool_cnt = 8192) {
    BufferPool::Instance().Init(buf_pool_cnt);
    impl_ = std::make_unique<AsyncLogger>(cfg);
  }

  void AddSink(std::shared_ptr<ILogSink> sink) {
    if (impl_) {
      impl_->AddSink(sink);
    }
  }

  void Flush() {
    if (impl_) {
      impl_->Flush();
    }
  }

  void Sync() {
    if (impl_) {
      impl_->Sync();
    }
  }

  AsyncLogger *GetImpl() { return impl_.get(); }
  size_t GetDropCount() const { return impl_->GetDropCount(); }
  LogLevel GetLevel() const { return level_; }
  void SetLevel(LogLevel lv) { level_ = lv; }

private:
  std::unique_ptr<AsyncLogger> impl_;
  std::atomic<LogLevel> level_{LogLevel::INFO};
};

template <typename... Args>
void LogSubmit(LogLevel level, const char *file, int line,
               std::format_string<Args...> fmt, Args &&...args) {
  // 从池拿 Buffer
  auto *buf = BufferPool::Instance().Alloc();
  try {
    // payload 写到缓冲区
    auto result = std::format_to_n(buf->data, LogBuffer::SIZE - 1, fmt,
                                   std::forward<Args>(args)...);
    buf->length = result.size;
    buf->data[buf->length] = '\0';
  } catch (...) {
    buf->reset();
  }
  // 减少每次提交时的查询线程ID成本
  static thread_local size_t hash_tid_cache =
      LogMessage::hash_func(std::this_thread::get_id());

  LogMessage msg(level, file, line, hash_tid_cache, buf);

  if (auto *logger = Logger::Instance().GetImpl(); logger != nullptr) {
    if (!logger->Commit(std::move(msg))) {
      logger->AddDropCount(1);
    }
  }
}

} // namespace lyf