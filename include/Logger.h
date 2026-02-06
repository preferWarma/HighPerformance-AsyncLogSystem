#pragma once

#include "LogConfig.h"
#include "Logger_impl.h"
#include "tool/Singleton.h"
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
  ~Logger() override { Shutdown(); }

  void Init(const LogConfig &cfg) {
    owned_config_ = cfg;
    buffer_pool_ =
        std::make_unique<BufferPool>(owned_config_.GetBufferPoolSize());
    impl_ = std::make_unique<AsyncLogger>(owned_config_);
    SetLevel(owned_config_.GetLevel());
  }

  void
  InitFromConfig(std::string_view config_path = LogConfig::kDefaultConfigPath) {
    owned_config_.LoadFromFile(config_path);
    owned_config_.StartHotReload(config_path);
    buffer_pool_ =
        std::make_unique<BufferPool>(owned_config_.GetBufferPoolSize());
    impl_ = std::make_unique<AsyncLogger>(owned_config_);
    SetLevel(owned_config_.GetLevel());
    auto file_path = owned_config_.GetLogPath();
    if (!file_path.empty()) {
      CreateLogDirectory(std::string(file_path));
      AddSink(std::make_shared<FileSink>(file_path, owned_config_));
    }
  }

  void AddSink(std::shared_ptr<ILogSink> sink) {
    if (impl_) {
      sink->ApplyConfig(owned_config_);
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

  void Shutdown() {
    if (impl_) {
      impl_->Sync();
    }
    impl_.reset();
    buffer_pool_.reset();
    owned_config_.StopHotReload();
  }

  AsyncLogger *GetImpl() { return impl_.get(); }
  BufferPool *GetBufferPool() { return buffer_pool_.get(); }
  size_t GetDropCount() const { return impl_->GetDropCount(); }
  LogLevel GetLevel() const { return owned_config_.GetLevel(); }
  void SetLevel(LogLevel lv) { owned_config_.SetLevel(lv); }
  const LogConfig &GetConfig() const { return owned_config_; }

private:
  std::unique_ptr<BufferPool> buffer_pool_;
  std::unique_ptr<AsyncLogger> impl_;
  LogConfig owned_config_;
};

// 线程局部缓存管理器
class ThreadLocalBufferCache {
private:
  std::vector<LogBuffer *> cache;
  BufferPool *pool_;
  size_t batch_size_;

public:
  ThreadLocalBufferCache(BufferPool *pool, size_t batch_size)
      : pool_(pool), batch_size_(batch_size) {}

  ~ThreadLocalBufferCache() {
    // 线程退出，归还所有缓存
    if (!cache.empty() && pool_) {
      pool_->FreeBatch(cache);
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
    cache.reserve(batch_size_);
    size_t count = pool_->AllocBatch(cache, batch_size_);

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
  auto *logger = Logger::Instance().GetImpl();
  auto *buffer_pool = Logger::Instance().GetBufferPool();
  if (logger == nullptr || buffer_pool == nullptr) {
    return;
  }

  auto tls_count = Logger::Instance().GetConfig().GetTLSBufferCount();
  if (tls_count == 0) {
    tls_count = LogConfig::kDefaultTLSBufferCount;
  }
  static thread_local ThreadLocalBufferCache tls_buf_cache(buffer_pool,
                                                           tls_count);
  auto *buf = tls_buf_cache.Get();

  // payload 写到缓冲区
  auto result = std::format_to_n(buf->data, LogBuffer::SIZE - 1, fmt,
                                 std::forward<Args>(args)...);
  buf->length = result.size;
  buf->data[buf->length] = '\0';

  // 减少每次提交时的查询线程ID成本
  static thread_local size_t hash_tid_cache =
      LogMessage::hash_func(std::this_thread::get_id());

  auto now = logger->GetCoarseTime();
  LogMessage msg(level, file, line, hash_tid_cache, now, buf, buffer_pool);

  if (!logger->Commit(std::move(msg))) {
    logger->AddDropCount(1);
  }
}

} // namespace lyf
