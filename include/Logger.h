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
  void Init(const QueConfig &cfg, size_t buf_pool_cnt = 40960) {
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

// 线程局部缓存管理器
class ThreadLocalBufferCache {
private:
  std::vector<LogBuffer *> cache;
  static constexpr size_t kBatchSize = 64;

public:
  ~ThreadLocalBufferCache() {
    // 线程退出，归还所有缓存
    if (!cache.empty()) {
      BufferPool::Instance().FreeBatch(cache);
    }
  }

  LogBuffer *Get() {
    // 缓存有，直接拿
    if (!cache.empty()) {
      LogBuffer *buf = cache.back();
      cache.pop_back();
      buf->reset();
      return buf;
    }

    // 缓存空，去全局池批发
    cache.reserve(kBatchSize);
    size_t count = BufferPool::Instance().AllocBatch(cache, kBatchSize);

    if (count > 0) {
      LogBuffer *buf = cache.back();
      cache.pop_back();
      buf->reset();
      return buf;
    }

    // 全局池也没了，兜底
    return new LogBuffer();
  }
};

template <typename... Args>
void LogSubmit(LogLevel level, const char *file, int line,
               std::format_string<Args...> fmt, Args &&...args) {
  // 线程局部缓存, 避免每次都去全局池拿
  static thread_local ThreadLocalBufferCache tls_buf_cache;
  auto *buf = tls_buf_cache.Get();

  // payload 写到缓冲区
  auto result = std::format_to_n(buf->data, LogBuffer::SIZE - 1, fmt,
                                 std::forward<Args>(args)...);
  buf->length = result.size;
  buf->data[buf->length] = '\0';

  // 减少每次提交时的查询线程ID成本
  static thread_local size_t hash_tid_cache =
      LogMessage::hash_func(std::this_thread::get_id());

  auto *logger = Logger::Instance().GetImpl();
  if (logger == nullptr) {
    return;
  }

  auto now = logger->GetCoarseTime();
  LogMessage msg(level, file, line, hash_tid_cache, now, buf);

  if (!logger->Commit(std::move(msg))) {
    logger->AddDropCount(1);
  }
}

} // namespace lyf
