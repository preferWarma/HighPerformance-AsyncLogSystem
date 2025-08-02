#pragma once

#include "Config.h"
#include "Helper.h"
#include "LogQue.h"
#include "Singleton.h"

#if defined(CLOUD_INCLUDE)
#include "CloudUploader.h"
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <utility>

// 日志调用宏
#define LOG_DEBUG(...) lyf::AsyncLogSystem::GetInstance().Debug(__VA_ARGS__)
#define LOG_INFO(...) lyf::AsyncLogSystem::GetInstance().Info(__VA_ARGS__)
#define LOG_WARN(...) lyf::AsyncLogSystem::GetInstance().Warn(__VA_ARGS__)
#define LOG_ERROR(...) lyf::AsyncLogSystem::GetInstance().Error(__VA_ARGS__)
#define LOG_FATAL(...) lyf::AsyncLogSystem::GetInstance().Fatal(__VA_ARGS__)

namespace lyf {
using steady_clock = std::chrono::steady_clock;
using std::unique_ptr, std::make_unique, std::mutex, std::lock_guard;

class AsyncLogSystem : public Singleton<AsyncLogSystem> {
  friend class Singleton<AsyncLogSystem>;

private:
  AsyncLogSystem() : config_(Config::GetInstance()) { Init(); }

public:
  void Init() {
    // 还在运行, 不初始化
    if (!isStop_.exchange(false)) {
      return;
    }
    consoleQue_ = make_unique<LogQueue>(config_.basic.maxConsoleLogQueueSize);
    fileQue_ = make_unique<LogQueue>(config_.basic.maxFileLogQueueSize);
    rotationType_ = RotationType::BY_SIZE_AND_TIME;
    lastFileFlushTime_ = std::chrono::steady_clock::now();
    rotateCounts_ = 0;

    if (config_.output.toFile) {
      if (initializeLogFile()) {
        std::cout << "Log system initialized. Current log file: "
                  << currentLogFilePath_ << std::endl;
      } else {
        std::cerr << "Failed to initialize log file." << std::endl;
        config_.output.toFile = false;
      }
    }

    if (config_.cloud.enable) {
#if defined(CLOUD_INCLUDE)
      cloudUploader_ = make_unique<CloudUploader>();
      cloudUploader_->start();

      if (!cloudUploader_->ping()) {
        std::cerr << "[Cloud] Cloud upload enabled, serverUrl:"
                  << config_.cloud.serverUrl << " is not available."
                  << std::endl;
        config_.cloud.enable = false;
      } else {
        std::cout << "[Cloud] Cloud upload enabled, serverUrl:"
                  << config_.cloud.serverUrl << std::endl;
      }
#endif
    }

    // 启动控制台输出线程
    if (config_.output.toConsole) {
      consoleWorker_ = thread(&AsyncLogSystem::ConsoleWorkerLoop, this);
    }
    // 启动文件输出线程
    if (config_.output.toFile) {
      fileWorker_ = thread(&AsyncLogSystem::FileWorkerLoop, this);
    }
  }

  // 停止日志系统
  inline void Stop() {
    bool expected = false;
    if (!isStop_.compare_exchange_strong(expected, true)) {
      return;
    }

    consoleQue_->Stop();
    fileQue_->Stop();

    // 等待工作线程完成
    if (consoleWorker_.joinable()) {
      consoleWorker_.join();
    }
    if (fileWorker_.joinable()) {
      fileWorker_.join();
    }

    // 最终flush
    if (config_.output.toFile) {
      lock_guard<mutex> lock(fileMtx_);
      if (logFile_.is_open()) {
        logFile_.flush();
      }
    }
    // 停止云端上传器
#if defined(CLOUD_INCLUDE)
    if (cloudUploader_) {
      // 轮转过，把最新的文件上传
      if (rotateCounts_ > 0) {
        cloudUploader_->uploadFileSync(currentLogFilePath_);
      }
      cloudUploader_->stop();
    }
#endif

    if (logFile_.is_open()) {
      logFile_.close();
    }

    // 输出系统关闭信息到控制台
    if (config_.output.toConsole) {
      // 红色字体
      std::cout << "\033[1;31m"
                << "[" << getCurrentTime() << "] [SYSTEM] Log system closed."
                << "\033[0m" << std::endl;
    }
  }

  // 刷新日志文件
  inline void Flush() {
    if (config_.output.toConsole) {
      std::cout.flush();
    }

    // 刷新日志文件
    if (config_.output.toFile) {
      lock_guard<mutex> lock(fileMtx_);
      if (logFile_.is_open()) {
        logFile_.flush();
      }
    }
  }

  ~AsyncLogSystem() noexcept { Stop(); }

public:
  template <typename... Args>
  void Log(LogLevel level, const string &fmt, Args &&...args) {
    if (isStop_ || static_cast<int>(level) < config_.output.minLogLevel) {
      return;
    }
    auto msg =
        LogMessage(level, FormatMessage(fmt, std::forward<Args>(args)...));

    // 分发到不同对列
    if (config_.output.toConsole) {
      consoleQue_->Push(LogMessage(msg)); // 复制一份给控制台
    }
    if (config_.output.toFile) {
      fileQue_->Push(std::move(msg)); // 直接移动给文件队列,提高性能
    }
  }

  // 便捷操作
  template <typename... Args>
  inline void Debug(const string &fmt, Args &&...args) {
    Log(LogLevel::DEBUG, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline void Info(const string &fmt, Args &&...args) {
    Log(LogLevel::INFO, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline void Warn(const string &fmt, Args &&...args) {
    Log(LogLevel::WARN, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline void Error(const string &fmt, Args &&...args) {
    Log(LogLevel::ERROR, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline void Fatal(const string &fmt, Args &&...args) {
    Log(LogLevel::FATAL, fmt, std::forward<Args>(args)...);
  }

public:
  string getCurrentLogFilePath() const {
    // 获取完整的绝对路径
    return currentLogFilePath_;
  }

  // 设置轮转参数的接口
  void setRotationType(RotationType type) { rotationType_ = type; }

  void setMaxFileSize(size_t maxSize) {
    config_.rotation.maxFileSize = maxSize;
  }

  void setMaxFileCount(int maxCount) {
    config_.rotation.maxFileCount = maxCount;
  }

  // 手动触发轮转
  void forceRotation() {
    if (rotateLogFile()) {
      std::cout << "Manual log rotation completed. New file: "
                << currentLogFilePath_ << std::endl;
    }
  }

  // 获取日志目录中的所有日志文件
  std::vector<std::string> getLogFiles() const {
    std::vector<std::string> files;
    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(config_.output.logRootDir)) {
        if (entry.is_regular_file()) {
          std::string filename = entry.path().filename().string();
          if (filename.starts_with("log_") && filename.ends_with(".txt")) {
            files.push_back(entry.path().string());
          }
        }
      }

      // 按文件名排序
      std::sort(files.begin(), files.end());
    } catch (const std::exception &e) {
      std::cerr << "Error listing log files: " << e.what() << std::endl;
    }
    return files;
  }

  // 生成新的日志文件名
  std::string generateLogFileName() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "log_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S_")
       << rotateCounts_ << ".txt";
    return (std::filesystem::path(config_.output.logRootDir) / ss.str())
        .string();
  }

  // 生成按日期的日志文件名 (用于按时间轮转)
  std::string generateDailyLogFileName(const std::string &date = "") const {
    std::string dateStr = date;
    if (dateStr.empty()) {
      auto now = std::chrono::system_clock::now();
      auto time_t = std::chrono::system_clock::to_time_t(now);
      std::stringstream ss;
      ss << std::put_time(std::localtime(&time_t), "%Y%m%d");
      dateStr = ss.str();
    }
    return (std::filesystem::path(config_.output.logRootDir) /
            ("log_" + dateStr + ".txt"))
        .string();
  }

  // 初始化日志文件
  bool initializeLogFile() {
    try {
      // 创建日志目录
      if (!std::filesystem::exists(config_.output.logRootDir)) {
        std::filesystem::create_directories(config_.output.logRootDir);
        std::cout << "Created log directory: " << config_.output.logRootDir
                  << std::endl;
      }
      // 根据轮转类型生成文件名
      if (rotationType_ == RotationType::BY_TIME ||
          rotationType_ == RotationType::BY_SIZE_AND_TIME) {
        currentLogFilePath_ = generateDailyLogFileName();
        lastRotationDate_ = getCurrentTime("%Y%m%d");
      } else {
        currentLogFilePath_ = generateLogFileName();
      }
      // 打开日志文件
      logFile_.open(currentLogFilePath_, std::ios::app | std::ios::out);
      if (!logFile_.is_open()) {
        std::cerr << "Failed to open log file: " << currentLogFilePath_
                  << std::endl;
        return false;
      }
      // 写入启动信息
      logFile_ << "[" << getCurrentTime()
               << "] [SYSTEM] Log system started. File: " << currentLogFilePath_
               << std::endl;
      logFile_.flush();
      currentFileWrittenBytes_ =
          std::filesystem::file_size(currentLogFilePath_);
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error initializing log file: " << e.what() << std::endl;
      return false;
    }
  }

  // 检查是否需要轮转
  bool needsRotation() {
    if (!config_.output.toFile || !logFile_.is_open()) {
      return false;
    }
    try {
      bool needRotation = false;
      // 检查文件大小
      if (rotationType_ == RotationType::BY_SIZE ||
          rotationType_ == RotationType::BY_SIZE_AND_TIME) {
        if (currentFileWrittenBytes_.load(std::memory_order_relaxed) >=
            config_.rotation.maxFileSize) {
          needRotation = true;
        }
      }
      // 检查日期变化
      if (rotationType_ == RotationType::BY_TIME ||
          rotationType_ == RotationType::BY_SIZE_AND_TIME) {
        std::string currentDate = getCurrentTime("%Y%m%d");
        if (currentDate != lastRotationDate_) {
          needRotation = true;
        }
      }
      return needRotation;
    } catch (const std::exception &e) {
      std::cerr << "Error checking rotation needs: " << e.what() << std::endl;
      return false;
    }
  }

  // 执行日志轮转
  bool rotateLogFile() {
    if (!config_.output.toFile) {
      return true;
    }
    // 使用原子标志避免重复轮转
    bool expected = false;
    if (!isRotating_.compare_exchange_strong(expected, true)) {
      return false; // 已经在轮转中
    }
    FlagGuard guard(isRotating_);

    try {
      std::lock_guard<std::mutex> lock(fileMtx_);
      // 写入轮转信息到当前文件
      if (logFile_.is_open()) {
        logFile_ << "[" << getCurrentTime()
                 << "] [SYSTEM] Log rotation triggered." << std::endl;
        logFile_.close();
      }
      // 生成新的日志文件名
      std::string newLogFile;
      if (rotationType_ == RotationType::BY_TIME ||
          rotationType_ == RotationType::BY_SIZE_AND_TIME) {
        std::string currentDate = getCurrentTime("%Y%m%d");
        if (currentDate != lastRotationDate_) {
          // 按日期轮转
          newLogFile = generateDailyLogFileName(currentDate);
          lastRotationDate_ = currentDate;
        } else {
          // 同一天内按大小轮转，添加序号
          newLogFile = generateLogFileName();
        }
      } else {
        newLogFile = generateLogFileName();
      }

      string oldLogFilePath = currentLogFilePath_;
      currentLogFilePath_ = newLogFile;
      // 打开新的日志文件
      logFile_.open(currentLogFilePath_, std::ios::app | std::ios::out);
      if (!logFile_.is_open()) {
        std::cerr << "Failed to open new log file: " << currentLogFilePath_
                  << std::endl;
        return false;
      }
      // 写入新文件启动信息
      logFile_ << "[" << getCurrentTime()
               << "] [SYSTEM] New log file created after rotation."
               << std::endl;
      logFile_.flush();
#if defined(CLOUD_INCLUDE)
      // 上传旧日志文件
      if (std::filesystem::exists(oldLogFilePath) && isCloudUploadEnabled()) {
        bool success = cloudUploader_->uploadFileSync(oldLogFilePath);
        if (success) {
          if (config_.cloud.deleteAfterUpload) {
            std::filesystem::remove(oldLogFilePath);
          }
          std::cout << "Uploaded old log file: " << oldLogFilePath << std::endl;
        } else {
          std::cerr << "Failed to upload old log file: " << oldLogFilePath
                    << std::endl;
        }
      }
#endif
      // 清理旧日志文件
      cleanupOldLogFiles();
      // 增加轮转次数
      ++rotateCounts_;
      currentFileWrittenBytes_.store(0, std::memory_order_relaxed);
      return true;

    } catch (const std::exception &e) {
      std::cerr << "Error during log rotation: " << e.what() << std::endl;
      return false;
    }
  }

  // 清理旧的日志文件
  void cleanupOldLogFiles() {
    try {
      std::vector<std::filesystem::path> logFiles;
      // 收集所有日志文件
      for (const auto &entry :
           std::filesystem::directory_iterator(config_.output.logRootDir)) {
        if (entry.is_regular_file()) {
          std::string filename = entry.path().filename().string();
          if (filename.starts_with("log_") && filename.ends_with(".txt")) {
            logFiles.push_back(entry.path());
          }
        }
      }
      // 按修改时间排序（最新的在前）
      std::sort(logFiles.begin(), logFiles.end(),
                [](const auto &a, const auto &b) {
                  return std::filesystem::last_write_time(a) >
                         std::filesystem::last_write_time(b);
                });
      // 删除超出数量限制的文件
      for (size_t i = config_.rotation.maxFileCount; i < logFiles.size(); ++i) {
        try {
          std::filesystem::remove(logFiles[i]);
          std::cout << "Removed old log file: " << logFiles[i] << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "Failed to remove old log file " << logFiles[i] << ": "
                    << e.what() << std::endl;
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Error during log cleanup: " << e.what() << std::endl;
    }
  }

#if defined(CLOUD_INCLUDE)
  // 获取上传队列状态
  size_t getUploadQueueSize() const {
    if (!cloudUploader_) {
      return 0;
    }
    return cloudUploader_->getQueueSize();
  }

  const auto &getCloudUploader() const { return cloudUploader_; }

  // 检查云端上传是否启用
  bool isCloudUploadEnabled() const {
    return config_.cloud.enable && cloudUploader_ != nullptr;
  }
#endif

private:
  // 控制台输出线程
  inline void ConsoleWorkerLoop() {
    LogQueue::QueueType batchQueue; // 当前批次的日志消息队列
    std::string buffer;
    buffer.reserve(1024 * 16); // 预分配16KB缓冲区

    auto work = [&]() {
      ProcessConsoleBatch(batchQueue, buffer);
      batchQueue = LogQueue::QueueType();
      buffer.clear();
    };

    while (!isStop_.load(std::memory_order_relaxed)) {
      if (consoleQue_->PopAll(batchQueue) > 0) {
        work();
      } else {
        // 队列为空，短暂休眠避免忙等待
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    // 处理剩余的日志消息
    while (consoleQue_->PopAll(batchQueue) > 0) {
      work();
    }
  }

  inline void FileWorkerLoop() {
    LogQueue::QueueType batchQueue; // 当前批次的日志消息队列
    std::string buffer;
    buffer.reserve(1024 * 64); // 预分配64KB缓冲区

    auto work = [&]() {
      ProcessFileBatch(batchQueue, buffer);
      batchQueue = LogQueue::QueueType();
      buffer.clear();
    };

    while (!isStop_.load(std::memory_order_relaxed)) {
      if (fileQue_->PopAll(batchQueue) > 0) {
        work();
      } else {
        // 队列为空，短暂休眠避免忙等待
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    // 处理剩余的日志消息
    while (fileQue_->PopAll(batchQueue) > 0) {
      work();
    }
  }

  inline void ProcessConsoleBatch(LogQueue::QueueType &batchQueue,
                                  std::string &buffer) {
    if (batchQueue.empty()) {
      return;
    }
    size_t processedCount = 0;
    while (!batchQueue.empty()) {
      auto msg = std::move(batchQueue.front());
      batchQueue.pop_front();

      buffer += LevelColor(msg.level);
      buffer +=
          "[" + formatTime(msg.time) + "] [" + LevelToString(msg.level) + "] ";
      buffer += msg.content;
      buffer += "\033[0m\n";

      ++processedCount;
      // 按批次大小输出, 避免缓冲区过大
      if (processedCount >= config_.performance.consoleBatchSize) {
        if (config_.performance.enableAsyncConsole) {
          std::cout << buffer; // 异步输出, 不立即刷新
        } else {
          std::cout << buffer << std::flush;
        }
        buffer.clear();
        processedCount = 0;
      }
    }
    // 输出剩余内容
    if (!buffer.empty()) {
      std::cout << buffer << std::flush;
    }
  }

  inline void ProcessFileBatch(LogQueue::QueueType &batchQueue,
                               std::string &buffer) {
    if (batchQueue.empty()) {
      return;
    }

    auto writeFile = [&](const char *data, size_t size) {
      if (!logFile_.is_open() || size == 0) {
        return;
      }
      lock_guard<mutex> lock(fileMtx_);
      logFile_.write(data, size);
      currentFileWrittenBytes_.fetch_add(size, std::memory_order_relaxed);
    };

    while (!batchQueue.empty()) {
      if (needsRotation()) {
        if (!buffer.empty()) {
          writeFile(buffer.data(), buffer.size());
          buffer.clear();
        }
        if (rotateLogFile()) {
          std::cout << "Log rotated to: " << currentLogFilePath_ << std::endl;
        }
      }

      auto msg = std::move(batchQueue.front());
      batchQueue.pop_front();

      std::string msgStr = "[" + formatTime(msg.time) + "] [" +
                           LevelToString(msg.level) + "] " + msg.content + "\n";

      // 避免缓冲区无限增长
      if (buffer.size() + msgStr.size() > 1024 * 64) {
        writeFile(buffer.data(), buffer.size());
        buffer.clear();
      }
      buffer += msgStr;
    }

    if (!buffer.empty()) {
      writeFile(buffer.data(), buffer.size());
      buffer.clear();
    }

    auto now = std::chrono::steady_clock::now();
    if (now - lastFileFlushTime_ >= config_.performance.fileFlushInterval) {
      lock_guard<mutex> lock(fileMtx_);
      if (logFile_.is_open()) {
        logFile_.flush();
      }
      lastFileFlushTime_ = now;
    }
  }

private:
  Config &config_; // 配置引用

private:
  std::ofstream logFile_;                      // 当前的日志文件输出流
  atomic<bool> isStop_{true};                  // 是否关闭
  mutable mutex fileMtx_;                      // 用于文件操作的互斥锁
  steady_clock::time_point lastFileFlushTime_; // 上次文件刷新时间

private:
  thread consoleWorker_;            // 控制台输出线程
  thread fileWorker_;               // 文件输出线程
  unique_ptr<LogQueue> consoleQue_; // 控制台输出队列
  unique_ptr<LogQueue> fileQue_;    // 文件输出队列

private:
  // 日志文件轮转相关
  string currentLogFilePath_; // 当前日志文件完整路径
  RotationType rotationType_; // 轮转类型
  string lastRotationDate_;   // 上次轮转的日期(用于按时间轮转)
  atomic<bool> isRotating_;   // 是否正在轮转
  atomic<int> rotateCounts_;  // 轮转次数
  atomic<size_t> currentFileWrittenBytes_{0};

#if defined(CLOUD_INCLUDE)
private:
  unique_ptr<CloudUploader> cloudUploader_; // 云上传器
#endif

}; // class AsyncLogSystem

} // namespace lyf
