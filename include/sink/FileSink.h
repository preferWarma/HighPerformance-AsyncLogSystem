#pragma once

#include "LogSink.h"
#include "tool/FastFormater.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>

namespace lyf {

class FileSink : public ILogSink {
public:
  static constexpr std::string_view StaticGetName() { return "FileSink"; }
  enum class RotationType { BY_SIZE, BY_TIME, BY_SIZE_AND_TIME };

  FileSink() = default;
  ~FileSink() override { Shutdown(); }

  bool Initialize(const LogConfig &config) override {
    config_ = &config;
    rotationType_ = RotationType::BY_TIME;
    lastFlushTime_ = std::chrono::steady_clock::now();
    writeBuffer_.reserve(1024 * config.performance.fileBufferSize_kb);
    return InitializeLogFile();
  }

  void Write(const LogMessage &msg) override {
    std::lock_guard<std::mutex> lock(write_mtx_);

    if (NeedsRotation()) {
      FlushBufferToFile();
      RotateLogFile();
    }

    // 直接格式化到buffer
    FormatMessageTo(writeBuffer_, msg);

    // 缓冲区过大时写入文件
    if (writeBuffer_.size() >= writeThreshold_) {
      FlushBufferToFile();
    }

    // 定时刷新
    auto now = std::chrono::steady_clock::now();
    if (now - lastFlushTime_ >= config_->performance.fileFlushInterval) {
      FlushImpl();
      lastFlushTime_ = now;
    }
  }

  void WriteBatch(std::span<const LogMessage> messages) override {
    if (messages.empty())
      return;

    std::lock_guard<std::mutex> lock(write_mtx_);

    if (NeedsRotation()) {
      FlushBufferToFile();
      RotateLogFile();
    }

    // 预估空间
    writeBuffer_.reserve(writeBuffer_.size() + messages.size() * 256);

    for (const auto &msg : messages) {
      FormatMessageTo(writeBuffer_, msg);

      // 避免单次缓冲过大
      if (writeBuffer_.size() >= writeThreshold_) {
        FlushBufferToFile();
      }
    }

    FlushBufferToFile();
  }

  void Flush() override {
    std::lock_guard<std::mutex> lock(write_mtx_);
    FlushImpl();
  }

  void Shutdown() override {
    std::lock_guard<std::mutex> lock(write_mtx_);
    FlushImpl();
    if (logFile_.is_open()) {
      logFile_.close();
    }
  }

  std::string_view GetName() const override { return StaticGetName(); }

  bool SupportsAsync() const override { return true; }

  size_t GetRecommendedBatchSize() const override {
    return config_->performance.fileBatchSize;
  }

  void SetRotationType(RotationType type) { rotationType_ = type; }
  void ForceRotation() {
    std::lock_guard<std::mutex> lock(write_mtx_);
    FlushBufferToFile();
    RotateLogFile();
  }
  std::string_view GetCurrentFilePath() const { return currentFilePath_; }

private:
  bool InitializeLogFile() {
    try {
      if (!std::filesystem::exists(config_->output.logRootDir)) {
        std::filesystem::create_directories(config_->output.logRootDir);
      }

      if (rotationType_ == RotationType::BY_TIME ||
          rotationType_ == RotationType::BY_SIZE_AND_TIME) {
        currentFilePath_ = GenerateDailyLogFileName();
        lastRotationDate_ = GetCurrentTime("%Y%m%d");
      } else {
        currentFilePath_ = GenerateLogFileName();
      }

      logFile_.open(currentFilePath_,
                    std::ios::app | std::ios::out | std::ios::binary);
      if (!logFile_.is_open()) {
        return false;
      }

      // 设置大缓冲区,减少系统调用
      static char fileBuffer[64 * 1024];
      logFile_.rdbuf()->pubsetbuf(fileBuffer, sizeof(fileBuffer));

      currentFileSize_ = std::filesystem::file_size(currentFilePath_);
      return true;

    } catch (const std::exception &e) {
      std::cerr << "[FileSink] Init error: " << e.what() << std::endl;
      return false;
    }
  }

  bool NeedsRotation() const {
    if (rotationType_ == RotationType::BY_SIZE ||
        rotationType_ == RotationType::BY_SIZE_AND_TIME) {
      if (currentFileSize_ >= config_->rotation.maxFileSize) {
        return true;
      }
    }

    if (rotationType_ == RotationType::BY_TIME ||
        rotationType_ == RotationType::BY_SIZE_AND_TIME) {
      std::string currentDate = GetCurrentTime("%Y%m%d");
      if (currentDate != lastRotationDate_) {
        return true;
      }
    }

    return false;
  }

  bool RotateLogFile() {
    try {
      if (logFile_.is_open()) {
        logFile_.flush();
        logFile_.close();
      }

      std::string newFilePath;
      if (rotationType_ == RotationType::BY_TIME ||
          rotationType_ == RotationType::BY_SIZE_AND_TIME) {
        std::string currentDate = GetCurrentTime("%Y%m%d");
        if (currentDate != lastRotationDate_) {
          newFilePath = GenerateDailyLogFileName(currentDate);
          lastRotationDate_ = currentDate;
        } else {
          newFilePath = GenerateLogFileName();
        }
      } else {
        newFilePath = GenerateLogFileName();
      }

      currentFilePath_ = std::move(newFilePath);

      logFile_.open(currentFilePath_,
                    std::ios::app | std::ios::out | std::ios::binary);
      if (!logFile_.is_open()) {
        return false;
      }

      static char fileBuffer[64 * 1024];
      logFile_.rdbuf()->pubsetbuf(fileBuffer, sizeof(fileBuffer));

      currentFileSize_ = 0;
      rotationCount_++;

      CleanupOldLogFiles();

      return true;

    } catch (const std::exception &e) {
      std::cerr << "[FileSink] Rotation error: " << e.what() << std::endl;
      return false;
    }
  }

  void CleanupOldLogFiles() {
    try {
      std::vector<std::filesystem::path> logFiles;

      for (const auto &entry :
           std::filesystem::directory_iterator(config_->output.logRootDir)) {
        if (entry.is_regular_file()) {
          if (starts_with(entry.path().filename().string(), "log_") &&
              ends_with(entry.path().filename().string(), ".log")) {
            logFiles.push_back(entry.path());
          }
        }
      }

      std::sort(logFiles.begin(), logFiles.end(),
                [](const auto &a, const auto &b) {
                  return std::filesystem::last_write_time(a) >
                         std::filesystem::last_write_time(b);
                });

      for (size_t i = config_->rotation.maxFileCount; i < logFiles.size();
           ++i) {
        std::filesystem::remove(logFiles[i]);
      }

    } catch (const std::exception &e) {
      std::cerr << "[FileSink] Cleanup error: " << e.what() << std::endl;
    }
  }

  std::string GenerateLogFileName() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "log_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S_")
       << rotationCount_ << ".log";
    return (std::filesystem::path(config_->output.logRootDir) / ss.str())
        .string();
  }

  std::string GenerateDailyLogFileName(const std::string &date = "") const {
    std::string dateStr = date.empty() ? GetCurrentTime("%Y%m%d") : date;
    return (std::filesystem::path(config_->output.logRootDir) /
            ("log_" + dateStr + ".log"))
        .string();
  }

  // 将缓冲区内容写入文件
  void FlushBufferToFile() {
    if (!writeBuffer_.empty() && logFile_.is_open()) {
      logFile_.write(writeBuffer_.data(), writeBuffer_.size());
      currentFileSize_ += writeBuffer_.size();
      writeBuffer_.clear();
    }
  }

  void FlushImpl() {
    FlushBufferToFile();
    if (logFile_.is_open()) {
      logFile_.flush();
    }
  }

  // 零拷贝格式化消息到buffer
  void FormatMessageTo(std::string &buf, const LogMessage &msg) {
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
    buf.push_back('\n');
  }

  template <typename T> void FastAppend(std::string &buf, T value) {
    detail::FastAppender<T>::append(buf, std::forward<T>(value));
  }

private:
  const LogConfig *config_ = nullptr;
  std::mutex write_mtx_;
  std::ofstream logFile_;
  std::string currentFilePath_;
  std::string writeBuffer_; // 写入缓冲区
  RotationType rotationType_;
  std::string lastRotationDate_;
  size_t currentFileSize_ = 0;
  int rotationCount_ = 0;
  std::chrono::steady_clock::time_point lastFlushTime_;
  size_t writeThreshold_ = 32 * 1024; // 32KB写入阈值
};

} // namespace lyf