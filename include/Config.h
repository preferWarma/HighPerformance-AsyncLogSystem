#pragma once
#include "tool/Timer.h"
#include "tool/Utility.h"
#include <filesystem>
#include <string>
#include <string_view>

namespace lyf {
using std::string;
namespace fs = std::filesystem;
namespace chrono = std::chrono;
using namespace std::chrono_literals;

enum class QueueFullPolicy {
  BLOCK = 0, // 队列满时阻塞
  DROP = 1   // 队列满时丢弃
};

struct Config {
  static Timer timer; // 异步定时器
private:
  std::string cfg_path_;               // 配置文件路径
  size_t listen_id_ = -1;              // 监听ID
  fs::file_time_type last_write_time_; // 上次配置文件修改时间

public:
  struct BasicConfig {
    size_t maxQueueSize; // 日志缓存队列初始时最大长度限制(0表示无限制)
    QueueFullPolicy queueFullPolicy; // 队列满时的处理策略(默认:BLOCK)
    size_t maxBlockTime_us; // 队列满时主线程阻塞最大时间，超过则会自动扩容
    size_t maxDropCount; // 队列满时丢弃最大数量，超过则会自动扩容
    size_t autoExpandMultiply; // 最大自动扩容倍数（以初始的最大队列长度为基准）
  } basic;

  struct OutputConfig {
    string logRootDir; // 日志根目录
    bool toFile;       // 是否输出到文件
    bool toConsole;    // 是否输出到控制台
    int minLogLevel; // 最小日志级别(0:DEBUG,1:INFO,2:WARNING,3:ERROR,4:FATAL)
  } output;

  struct PerformanceConfig {
    bool enableAsyncConsole;                   // 是否异步控制台输出
    size_t consoleBatchSize;                   // 控制台小批次
    size_t fileBatchSize;                      // 文件大批次
    chrono::milliseconds consoleFlushInterval; // 控制台刷新间隔
    chrono::milliseconds fileFlushInterval;    // 文件刷新间隔
    size_t consoleBufferSize_kb; // 控制台预分配缓冲区大小
    size_t fileBufferSize_kb;    // 文件预分配缓冲区大小
  } performance;

  struct RotationConfig {
    size_t maxFileSize;  // 最大文件大小
    size_t maxFileCount; // 保留最近文件数量
  } rotation;

  Config() = default;
  Config(std::string_view config_path, bool autoReload = false,
         size_t reloadInterval_ms = 1000)
      : cfg_path_(config_path) {
    Init(config_path);
    InitListener(config_path, reloadInterval_ms);
    if (autoReload) {
      StartListener();
    }
  }

  // 启动配置文件监听
  bool StartListener() { return Config::timer.resume(listen_id_); }
  bool StopListener() { return Config::timer.pause(listen_id_); }
  size_t GetListenID() const { return listen_id_; }

  Config(const Config &other) {
    basic = other.basic;
    output = other.output;
    performance = other.performance;
    rotation = other.rotation;
    cfg_path_ = other.cfg_path_;
    listen_id_ = other.listen_id_;
    last_write_time_ = other.last_write_time_;
  }
  Config &operator=(const Config &other) {
    if (this != &other) {
      basic = other.basic;
      output = other.output;
      performance = other.performance;
      rotation = other.rotation;
      cfg_path_ = other.cfg_path_;
      listen_id_ = other.listen_id_;
      last_write_time_ = other.last_write_time_;
    }
    return *this;
  }

private:
  void Init(std::string_view config_path) {
    auto cfg_data = toml::parse_file(config_path);
    { // basic config
      basic.maxQueueSize = cfg_data["basic"]["maxQueueSize"].value_or(0);
      basic.queueFullPolicy = cfg_data["basic"]["queueFullPolicy"]
                                          .value_or(std::string_view("BLOCK"))
                                          .compare("BLOCK") == 0
                                  ? QueueFullPolicy::BLOCK
                                  : QueueFullPolicy::DROP;
      basic.maxBlockTime_us = cfg_data["basic"]["maxBlockTime_us"].value_or(16);
      basic.maxDropCount = cfg_data["basic"]["maxDropCount"].value_or(40960);
      basic.autoExpandMultiply =
          cfg_data["basic"]["autoExpandMultiply"].value_or(4);
    }

    { // output config
      output.logRootDir = cfg_data["output"]["logRootDir"].value_or("./log");
      output.toFile = cfg_data["output"]["toFile"].value_or(true);
      output.toConsole = cfg_data["output"]["toConsole"].value_or(false);
      output.minLogLevel = cfg_data["output"]["minLogLevel"].value_or(0);
    }

    { // performance config
      performance.consoleBatchSize =
          cfg_data["performance"]["consoleBatchSize"].value_or(512);
      performance.fileBatchSize =
          cfg_data["performance"]["fileBatchSize"].value_or(1024);
      performance.consoleFlushInterval = chrono::milliseconds(
          cfg_data["performance"]["consoleFlushInterval_ms"].value_or(50));
      performance.fileFlushInterval = chrono::milliseconds(
          cfg_data["performance"]["fileFlushInterval_ms"].value_or(100));
      performance.enableAsyncConsole =
          cfg_data["performance"]["enableAsyncConsole"].value_or(true);
      performance.consoleBufferSize_kb =
          cfg_data["performance"]["consoleBufferSize_kb"].value_or(16);
      performance.fileBufferSize_kb =
          cfg_data["performance"]["fileBufferSize_kb"].value_or(32);
    }

    { // rotation config
      rotation.maxFileSize = cfg_data["rotation"]["maxLogFileSize"].value_or(
          1024 * 1024 * 10); // 默认10MB
      rotation.maxFileCount =
          cfg_data["rotation"]["maxLogFileCount"].value_or(10);
    }
  }

  size_t InitListener(std::string_view config_path, size_t reloadInterval_ms) {
    if (config_path.empty()) {
      return -1;
    }
    last_write_time_ = GetFileLastWriteTime(config_path);
    listen_id_ = Config::timer.setInterval(reloadInterval_ms, [&]() {
      auto newLastWriteTime = GetFileLastWriteTime(config_path);
      if (newLastWriteTime > last_write_time_) {
        lyf_inner_log("Config file '{}' has been modified, reloading.",
                      config_path);
        Init(config_path); // 重新初始化配置
        last_write_time_ = newLastWriteTime;
      }
    });
    // 初始时暂停监听
    Config::timer.pause(listen_id_);
    return listen_id_;
  }
};

inline Timer Config::timer;
} // namespace lyf