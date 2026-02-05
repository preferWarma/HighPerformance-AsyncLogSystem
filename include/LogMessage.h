#pragma once

#include "third/concurrentqueue.h" // ConcurrentQueue
#include "tool/Singleton.h"
#include <chrono>
#include <cstddef>
#include <future>
#include <thread>

namespace lyf {
enum class LogLevel {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
  FATAL = 4,
  FLUSH = 99, // 特殊级别，用于触发刷新
};

inline constexpr std::string_view LevelToString(LogLevel level) {
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

// 定长 Buffer，承载实际数据
struct LogBuffer {
  static constexpr size_t SIZE = 4096; // 4KB per log
  char data[SIZE];
  size_t length = 0;

  void reset() { length = 0; }

  // 提供给 std::format_to 的迭代器接口
  char *begin() { return data; }
  char *end() { return data + SIZE; }
};

// 内存池
class BufferPool : public Singleton<BufferPool> {
  friend class Singleton<BufferPool>;

public:
  void Init(size_t count = 10000) {
    for (size_t i = 0; i < count; ++i) {
      pool_.enqueue(new LogBuffer());
    }
  }

  LogBuffer *Alloc() {
    LogBuffer *buf;
    if (pool_.try_dequeue(buf)) {
      buf->reset();
      return buf;
    }
    return new LogBuffer();
  }

  void Free(LogBuffer *buf) {
    if (buf) {
      pool_.enqueue(buf);
    }
  }

private:
  moodycamel::ConcurrentQueue<LogBuffer *> pool_;
};

struct LogMessage {
  constexpr static std::hash<std::thread::id> hash_func;
  using system_clock = std::chrono::system_clock;

  system_clock::time_point time;
  LogLevel level;
  const char *file_name;
  size_t file_line;
  size_t hash_tid;
  LogBuffer *buffer_ptr = nullptr;            // 指向内存池中的 Buffer
  std::promise<void> *sync_promise = nullptr; // 用于通知主线程

  // 构造函数：接管 buffer
  LogMessage(LogLevel lv, const char *file, size_t line, size_t hash_tid,
             LogBuffer *buf)
      : time(system_clock::now()), level(lv), file_name(file), file_line(line),
        hash_tid(hash_tid), buffer_ptr(buf) {}
  LogMessage(LogLevel lv, const char *file, size_t line, std::thread::id tid,
             LogBuffer *buf)
      : time(system_clock::now()), level(lv), file_name(file), file_line(line),
        hash_tid(hash_func(tid)), buffer_ptr(buf) {}

  // FLUSH 指令专用构造函数
  LogMessage(std::promise<void> *prom)
      : level(LogLevel::FLUSH), sync_promise(prom), buffer_ptr(nullptr) {
    // FLUSH 消息不需要 buffer，也不需要文件名行号
  }

  // 禁用拷贝 (防止 Double Free)
  LogMessage(const LogMessage &) = delete;
  LogMessage &operator=(const LogMessage &) = delete;

  // 允许移动 (转移 buffer 所有权)
  LogMessage(LogMessage &&other) noexcept
      : time(other.time), level(other.level), file_name(other.file_name),
        file_line(other.file_line), hash_tid(other.hash_tid),
        buffer_ptr(other.buffer_ptr) {
    sync_promise = other.sync_promise;
    buffer_ptr = other.buffer_ptr;
    other.sync_promise = nullptr;
    other.buffer_ptr = nullptr;
  }

  LogMessage &operator=(LogMessage &&other) noexcept {
    if (this != &other) {
      if (buffer_ptr) {
        BufferPool::Instance().Free(buffer_ptr); // 释放旧的
      }
      // 复制元数据
      time = other.time;
      level = other.level;
      file_name = other.file_name;
      file_line = other.file_line;
      hash_tid = other.hash_tid;
      // 转移 Buffer 所有权
      buffer_ptr = other.buffer_ptr;
      other.buffer_ptr = nullptr;
      sync_promise = other.sync_promise;
      other.sync_promise = nullptr;
    }
    return *this;
  }

  // 析构时自动归还 Buffer 到池子
  ~LogMessage() {
    if (buffer_ptr) {
      BufferPool::Instance().Free(buffer_ptr);
    }
  }

  // Buffer 内容视图（只读）
  std::string_view GetContent() const {
    if (!buffer_ptr) {
      return {};
    }
    return std::string_view(buffer_ptr->data, buffer_ptr->length);
  }
};
} // namespace lyf