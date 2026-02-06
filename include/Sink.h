#pragma once

#include "LogConfig.h"
#include "tool/LogFormatter.h"
#include <string_view>

namespace lyf {

class ILogSink {
public:
  virtual ~ILogSink() = default;
  virtual void Log(const LogMessage &msg) = 0;
  virtual void Flush() = 0;

  virtual void ApplyConfig(const LogConfig &config) {
    formatter_.SetConfig(&config);
  }

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

    auto file_buffer_size = LogConfig::kDefaultFileBufferSize;
    // 分配文件缓冲，避免频繁系统调用
    setvbuf(file_, nullptr, _IOFBF, file_buffer_size);
    buffer_.reserve(file_buffer_size);
  }

  FileSink(std::string_view filepath, const LogConfig &config) {
    file_ = fopen(filepath.data(), "a");
    ApplyConfig(config);
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

  void ApplyConfig(const LogConfig &config) override {
    formatter_.SetConfig(&config);
    if (file_) {
      auto file_buffer_size = config.GetFileBufferSize();
      setvbuf(file_, nullptr, _IOFBF, file_buffer_size);
      buffer_.reserve(file_buffer_size);
    }
  }

private:
  FILE *file_ = nullptr;
};

// --- 控制台 Sink 实现 ---
class ConsoleSink : public ILogSink {
public:
  ConsoleSink() { buffer_.reserve(LogConfig::kDefaultConsoleBufferSize); }
  explicit ConsoleSink(const LogConfig &config) { ApplyConfig(config); }

  void Log(const LogMessage &msg) override {
    buffer_.clear();
    formatter_.Format(msg, buffer_);
    fwrite(buffer_.data(), 1, buffer_.size(), stdout);
  }

  void Flush() override { fflush(stdout); }

  void ApplyConfig(const LogConfig &config) override {
    formatter_.SetConfig(&config);
    buffer_.reserve(config.GetConsoleBufferSize());
  }
};

} // namespace lyf
