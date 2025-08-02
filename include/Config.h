#pragma once
#include "ConfigManager.h"
#include "Singleton.h"
#include <string>

// 条件编译，减少第三方依赖，便于移植和快速使用
// #define CLOUD_INCLUDE

namespace lyf {
using std::string;
using std::chrono::milliseconds;

struct Config : Singleton<Config> {
  friend class Singleton<Config>;

  struct BasicConfig {
    size_t maxConsoleLogQueueSize; // 最大控制台日志队列大小
    size_t maxFileLogQueueSize;    // 最大文件日志队列大小
  } basic;

  struct OutPutConfig {
    string logRootDir; // 日志根目录
    bool toFile;       // 是否输出到文件
    bool toConsole;    // 是否输出到控制台
    int minLogLevel;   // 最小日志级别(0:DEBUG,1:INFO,2:WARNING,3:ERROR,4:FATAL)
  } output;

  struct PerformanceConfig {
    bool enableAsyncConsole;           // 是否异步控制台输出
    size_t consoleBatchSize;           // 控制台小批次
    size_t fileBatchSize;              // 文件大批次
    milliseconds consoleFlushInterval; // 控制台刷新间隔
    milliseconds fileFlushInterval;    // 文件刷新间隔
  } performance;

  struct RotationConfig {
    size_t maxFileSize;  // 最大文件大小
    size_t maxFileCount; // 保留最近文件数量
  } rotation;

  struct CloudConfig {
    bool enable;            // 是否启用云存储
    string serverUrl;       // 云存储服务器URL
    string uploadEndpoint;  // 上传接口路径
    int uploadTimeout_s;    // 上传超时时间（秒）
    bool deleteAfterUpload; // 上传后是否删除本地文件
    string apiKey;          // API密钥
    int maxRetries;         // 最大重试次数
    int retryDelay_s;       // 重试延迟时间（秒）
    int maxQueueSize;       // 上传队列最大大小
  } cloud;

  Config() { Init(); }

  void Init() {
    auto &manager = ConfigManagerImpl::GetInstance();
    { // basic config
      basic.maxConsoleLogQueueSize =
          manager.get<int>("basic.maxConsoleLogQueueSize");
      basic.maxFileLogQueueSize = manager.get<int>("basic.maxFileLogQueueSize");
    }

    { // output config
      output.logRootDir = manager.get<string>("output.logRootDir");
      output.toFile = manager.get<bool>("output.toFile");
      output.toConsole = manager.get<bool>("output.toConsole");
      output.minLogLevel = manager.get<int>("output.minLogLevel");
    }

    { // performance config
      performance.consoleBatchSize =
          manager.get<int>("performance.consoleBatchSize");
      performance.fileBatchSize = manager.get<int>("performance.fileBatchSize");
      performance.consoleFlushInterval =
          milliseconds(manager.get<int>("performance.consoleFlushInterval_ms"));
      performance.fileFlushInterval =
          milliseconds(manager.get<int>("performance.fileFlushInterval_ms"));
      performance.enableAsyncConsole =
          manager.get<bool>("performance.enableAsyncConsole");
    }

    { // rotation config
      rotation.maxFileSize = manager.get<int>("rotation.maxLogFileSize");
      rotation.maxFileCount = manager.get<int>("rotation.maxLogFileCount");
    }

    { // cloud config
      cloud.enable = manager.get<bool>("cloud.enable");
      cloud.serverUrl = manager.get<string>("cloud.serverUrl");
      cloud.uploadEndpoint = manager.get<string>("cloud.uploadEndpoint");
      cloud.uploadTimeout_s = manager.get<int>("cloud.uploadTimeout_s");
      cloud.deleteAfterUpload = manager.get<bool>("cloud.deleteAfterUpload");
      cloud.apiKey = manager.get<string>("cloud.apiKey");
      cloud.maxRetries = manager.get<int>("cloud.maxRetries");
      cloud.retryDelay_s = manager.get<int>("cloud.retryDelay_s");
      cloud.maxQueueSize = manager.get<int>("cloud.maxQueueSize");
    }
  }
};
} // namespace lyf