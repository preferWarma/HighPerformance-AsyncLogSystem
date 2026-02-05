#pragma once

#include "tool/LogFormatter.h"
#include <string_view>

namespace lyf {

class ILogSink {
public:
  virtual ~ILogSink() = default;
  virtual void Log(const LogMessage &msg) = 0;
  virtual void Flush() = 0;

protected:
  // 每个 Sink 拥有自己的 Formatter 和 暂存 Buffer
  LogFormatter formatter_;
  std::vector<char> buffer_;
};

// --- 文件 Sink 实现 ---
class FileSink : public ILogSink {
public:
  FileSink(std::string_view filepath) {
    file_ = fopen(filepath.data(), "a");
    constexpr size_t kBufferSize = 128 * 1024;
    // 分配文件缓冲，避免频繁系统调用
    setvbuf(file_, nullptr, _IOFBF, kBufferSize);
    // 预分配 Buffer 空间，避免频繁 realloc
    buffer_.reserve(kBufferSize);
  }

  ~FileSink() {
    Flush();
    if (file_) {
      fclose(file_);
    }
  }

  void Log(const LogMessage &msg) override {
    if (!file_) {
      return;
    }
    buffer_.clear();
    formatter_.Format(msg, buffer_);
    fwrite(buffer_.data(), 1, buffer_.size(), file_);
  }

  void Flush() override {
    if (file_) {
      fflush(file_);
    }
  }

private:
  FILE *file_ = nullptr;
};

// --- 控制台 Sink 实现 ---
class ConsoleSink : public ILogSink {
public:
  ConsoleSink() { buffer_.reserve(1024); }

  void Log(const LogMessage &msg) override {
    buffer_.clear();
    formatter_.Format(msg, buffer_);
    fwrite(buffer_.data(), 1, buffer_.size(), stdout);
  }

  void Flush() override { fflush(stdout); }
};

} // namespace lyf