#pragma once

#include "Config.h"
#include "LogQue.h"
#include "sink/ConsoleSink.h"
#include "sink/FileSink.h"
#include "sink/HttpSink.h"
#include "sink/SinkManager.h"
#include "tool/FastFormater.h"
#include "tool/Singleton.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

// 初始化日志系统
#define INIT_LOG_SYSTEM(cfg_path_)                                             \
  lyf::AsyncLogSystem::Instance().Init(lyf::LogConfig(cfg_path_))

// 日志调用宏
#if __cplusplus >= 202002L
#define LOG_IMPL(level, fmt, ...)                                              \
  lyf::AsyncLogSystem::Instance().Log<fmt>(                                    \
      level, __FILE__, __LINE__,                                               \
      std::hash<std::thread::id>()(std::this_thread::get_id()), ##__VA_ARGS__)
#else
#define LOG_IMPL(level, fmt, ...)                                              \
  lyf::AsyncLogSystem::Instance().Log(                                         \
      level, __FILE__, __LINE__,                                               \
      std::hash<std::thread::id>()(std::this_thread::get_id()), fmt,           \
      ##__VA_ARGS__)
#endif

#define LOG_DEBUG(fmt, ...) LOG_IMPL(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_IMPL(LogLevel::INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_IMPL(LogLevel::WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_IMPL(LogLevel::ERROR, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) LOG_IMPL(LogLevel::FATAL, fmt, ##__VA_ARGS__)

namespace lyf {
using steady_clock = std::chrono::steady_clock;
using std::unique_ptr, std::make_unique, std::mutex, std::lock_guard;

class AsyncLogSystem : public Singleton<AsyncLogSystem> {
  friend class Singleton<AsyncLogSystem>;

public:
  inline void Init(const LogConfig &config);
  inline LogConfig &GetConfig() { return config_; }
  inline bool HasInit() const { return !isStop_.load(); }
  inline void Stop();
  inline void Flush();
  inline std::string getCurrentLogFilePath() {
    auto fileSink = static_cast<FileSink *>(
        sinkManager_.FindSink(FileSink::StaticGetName()));
    return fileSink ? std::string(fileSink->GetCurrentFilePath()) : "";
  }
  ~AsyncLogSystem() noexcept { Stop(); }

public:
#if __cplusplus >= 202002L
  template <FixedString fmt, typename... Args>
  inline void Log(LogLevel level, const char *file_name, size_t file_line,
                  size_t thread_id, Args &&...args);
#else
  template <typename... Args>
  inline void Log(LogLevel level, const char *file_name, size_t file_line,
                  size_t thread_id, std::string_view fmt, Args &&...args);
#endif

  // =============sink管理================
  inline SinkManager &GetSinkManager() { return sinkManager_; }

  inline void RegisterSinkFactory(std::string name,
                                  std::unique_ptr<ILogSinkFactory> factory) {
    sinkManager_.RegisterSinkFactory(std::move(name), std::move(factory));
  }

  inline bool AddSink(std::string_view factoryName) {
    return sinkManager_.AddSink(factoryName, config_);
  }

  inline bool AddSink(std::unique_ptr<ILogSink> sink) {
    if (sink->Initialize(config_)) {
      sinkManager_.AddSink(std::move(sink));
      return true;
    }
    return false;
  }

private:
  inline void WorkerLoop();
  inline void ProcessBatch(LogQueue::OutputQueType &batchQueue);

private:
  LogConfig config_;
  SinkManager sinkManager_;

  atomic<bool> isStop_{true};
  unique_ptr<LogQueue> logQueue_;
  std::thread worker_;

  // 刷新相关
  mutable std::mutex flushSerializerMtx_;
  mutable std::mutex flushMtx_;
  std::condition_variable flushCv_;
  std::atomic<bool> flushPending_{false};
  const char *FLUSH_COMMAND = "__FLUSH_COMMAND__";

}; // class AsyncLogSystem

inline void AsyncLogSystem::Init(const LogConfig &config) {
  // 还在运行, 不初始化
  if (!isStop_.exchange(false)) {
    return;
  }

  config_ = config;
  logQueue_ = make_unique<LogQueue>(config_);

  // 注册内置Sink工厂
  RegisterSinkFactory("console", make_unique<LogSinkFactory<ConsoleSink>>());
  RegisterSinkFactory("file", make_unique<LogSinkFactory<FileSink>>());
  RegisterSinkFactory("http", make_unique<LogSinkFactory<HttpSink>>());

  // 根据配置添加Sink
  if (config_.output.toConsole) {
    if (AddSink("console")) {
      lyf_inner_log("[LogSystem] Console sink initialized.");
    }
  }

  if (config_.output.toFile) {
    if (AddSink("file")) {
      lyf_inner_log("[LogSystem] File sink initialized.");
    }
  }

  if (config_.http.toHttp) {
    auto httpSink = make_unique<HttpSink>();
    HttpSink::HttpConfig httpConfig;
    httpConfig.url = config_.http.url;
    httpConfig.endpoint = config_.http.endpoint;
    httpConfig.contentType = config_.http.contentType;
    httpConfig.timeout_sec = config_.http.timeout_sec;
    httpConfig.maxRetries = config_.http.maxRetries;
    httpConfig.batchSize = config_.http.batchSize;
    httpConfig.bufferSize_kb = config_.http.bufferSize_kb;
    httpConfig.enableCompression = config_.http.enableCompression;
    httpConfig.enableAsync = config_.http.enableAsync;
    httpSink->SetHttpConfig(httpConfig);

    if (AddSink(std::move(httpSink))) {
      lyf_inner_log("[LogSystem] HTTP sink initialized.");
    }
  }

  // 启动工作线程
  worker_ = std::thread(&AsyncLogSystem::WorkerLoop, this);

  lyf_inner_log("[LogSystem] Log system initialized with {} sinks.",
                sinkManager_.GetSinkCount());
}

inline void AsyncLogSystem::Stop() {
  bool expected = false;
  if (!isStop_.compare_exchange_strong(expected, true)) {
    return;
  }

  // 等待工作线程完成
  if (worker_.joinable()) {
    worker_.join();
  }

  // 关闭所有Sink
  sinkManager_.ShutdownAll();
  lyf_inner_log("[LogSystem] Log system closed.");
}

inline void AsyncLogSystem::Flush() {
  std::lock_guard<std::mutex> serializer_lock(flushSerializerMtx_);
  if (isStop_.load(std::memory_order_relaxed)) {
    return;
  }

  std::unique_lock<std::mutex> flush_lock(flushMtx_);

  // 发送刷新命令
  LogMessage flush_cmd(LogLevel::INFO, __FILE__, __LINE__,
                       std::hash<std::thread::id>()(std::this_thread::get_id()),
                       FLUSH_COMMAND);
  logQueue_->Push(std::move(flush_cmd));
  flushPending_.store(true);

  // 等待刷新完成
  flushCv_.wait(flush_lock, [this] { return !flushPending_.load(); });
}

inline void AsyncLogSystem::WorkerLoop() {
  LogQueue::OutputQueType batchQueue;
  std::vector<LogMessage> messageBatch; // 重用的batch容器
  messageBatch.reserve(config_.performance.consoleBatchSize);

  auto work = [&]() {
    ProcessBatch(batchQueue);
    batchQueue.clear();
  };

  int sleepTime_ms = 1;
  while (!isStop_.load(std::memory_order_relaxed)) {
    size_t count =
        logQueue_->PopBatch(batchQueue, config_.performance.consoleBatchSize);
    if (count > 0) {
      work();
      sleepTime_ms = 1;
    } else {
      // 队列为空,短暂休眠避免忙等待
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime_ms));
      sleepTime_ms = std::min(100, sleepTime_ms * 2);
    }
  }

  // 处理剩余的日志消息
  while (logQueue_->PopBatch(batchQueue) > 0) {
    work();
  }
}

inline void AsyncLogSystem::ProcessBatch(std::deque<LogMessage> &batchQueue) {
  if (batchQueue.empty()) {
    return;
  }

  // 使用静态vector避免重复分配
  static thread_local std::vector<LogMessage> messageBatch;
  messageBatch.clear();
  messageBatch.reserve(batchQueue.size());

  while (!batchQueue.empty()) {
    LogMessage msg = std::move(batchQueue.front());
    batchQueue.pop_front();

    // 处理刷新命令
    [[unlikely]] if (msg.content == FLUSH_COMMAND) {
      // 先写入当前批次
      if (!messageBatch.empty()) {
        // 使用span传递视图,零拷贝
        sinkManager_.WriteBatch(std::span<const LogMessage>(messageBatch));
        messageBatch.clear();
      }

      // 刷新所有Sink
      sinkManager_.FlushAll();

      // 通知等待线程
      flushPending_.store(false);
      std::lock_guard<std::mutex> lock(flushMtx_);
      flushCv_.notify_one();
      continue;
    }

    messageBatch.push_back(std::move(msg));
  }

  // 批量写入到所有Sink (使用span,零拷贝)
  if (!messageBatch.empty()) {
    sinkManager_.WriteBatch(std::span<const LogMessage>(messageBatch));
  }
}

#if __cplusplus >= 202002L
template <FixedString fmt, typename... Args>
inline void AsyncLogSystem::Log(LogLevel level, const char *file_name,
                                size_t file_line, size_t thread_id,
                                Args &&...args) {
  if (isStop_ || static_cast<int>(level) < config_.output.minLogLevel) {
    return;
  }
  logQueue_->Push(LogMessage(level, file_name, file_line, thread_id,
                             FormatMessage<fmt>(std::forward<Args>(args)...)));
}
#else
template <typename... Args>
inline void AsyncLogSystem::Log(LogLevel level, const char *file_name,
                                size_t file_line, size_t thread_id,
                                std::string_view fmt, Args &&...args) {
  if (isStop_ || static_cast<int>(level) < config_.output.minLogLevel) {
    return;
  }
  logQueue_->Push(LogMessage(level, file_name, file_line, thread_id,
                             FormatMessage(fmt, std::forward<Args>(args)...)));
}
#endif

} // namespace lyf