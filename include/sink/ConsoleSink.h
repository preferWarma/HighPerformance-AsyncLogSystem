#pragma once

#include "LogSink.h"
#include "tool/FastFormater.h"
#include <iostream>
#include <mutex>
#include <string_view>

namespace lyf {

class ConsoleSink : public ILogSink {
public:
  ConsoleSink() = default;
  ~ConsoleSink() override { Shutdown(); }

  static constexpr std::string_view StaticGetName() { return "ConsoleSink"; }

  bool Initialize(const LogConfig &config) override {
    config_ = &config;
    enableColor_ = true;
    enableAsync_ = config.performance.enableAsyncConsole;
    buffer_.reserve(1024 * config.performance.consoleBufferSize_kb);
    return true;
  }

  void Write(const LogMessage &msg) override {
    std::lock_guard<std::mutex> lock(write_mtx_);
    // 直接写入buffer,避免临时string
    FormatMessageTo(buffer_, msg);
    if (!enableAsync_ || buffer_.size() >= bufferThreshold_) {
      FlushImpl();
    }
  }

  void WriteBatch(std::span<const LogMessage> messages) override {
    if (messages.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(write_mtx_);
    // 预估空间需求,减少reallocation
    buffer_.reserve(buffer_.size() + messages.size() * 256);

    for (const auto &msg : messages) {
      FormatMessageTo(buffer_, msg);
    }

    FlushImpl();
  }

  void Flush() override {
    std::lock_guard<std::mutex> lock(write_mtx_);
    FlushImpl();
  }

  void Shutdown() override { Flush(); }
  std::string_view GetName() const override { return StaticGetName(); }
  bool SupportsAsync() const override { return true; }
  size_t GetRecommendedBatchSize() const override {
    return config_->performance.consoleBatchSize;
  }

private:
  void FlushImpl() {
    if (!buffer_.empty()) {
      // 一次性写入,减少系统调用
      std::cout.write(buffer_.data(), buffer_.size());
      std::cout.flush();
      buffer_.clear();
    }
  }

  // 直接格式化到buffer
  void FormatMessageTo(std::string &buf, const LogMessage &msg) {
    if (enableColor_) {
      buf.append(GetLevelColor(msg.level));
    }

    // 使用FastFormatter直接追加到buffer
    FastAppend(buf, FormatTime(msg.time));
    buf.push_back(' ');
    FastAppend(buf, LevelToString(msg.level));
    buf.push_back(' ');
    FastAppend(buf, msg.hash_tid);
    buf.push_back(' ');
    buf.append(msg.file_name);
    buf.push_back(':');
    FastAppend(buf, msg.file_line);
    buf.push_back(' ');
    buf.append(msg.content);

    if (enableColor_) {
      buf.append("\033[0m");
    }
    buf.push_back('\n');
  }

  static constexpr std::string_view GetLevelColor(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG:
      return "\033[0;37m";
    case LogLevel::INFO:
      return "\033[0;32m";
    case LogLevel::WARN:
      return "\033[1;33m";
    case LogLevel::ERROR:
      return "\033[1;31m";
    case LogLevel::FATAL:
      return "\033[1;35m";
    default:
      return "\033[0m";
    }
  }

  // 快速追加数字到string
  template <typename T> void FastAppend(std::string &buf, T value) {
    detail::FastAppender<T>::append(buf, std::forward<T>(value));
  }

private:
  const LogConfig *config_ = nullptr;
  std::mutex write_mtx_;
  std::string buffer_;
  bool enableColor_ = true;
  bool enableAsync_ = false;
  size_t bufferThreshold_ = 4096;
};

} // namespace lyf