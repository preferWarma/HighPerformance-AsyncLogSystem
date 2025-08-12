#pragma once
#include "JsonHelper.h"
#include "Singleton.h"
#include <string>

// 是否编译云存储模块
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
    int minLogLevel; // 最小日志级别(0:DEBUG,1:INFO,2:WARNING,3:ERROR,4:FATAL)
  } output;

  struct PerformanceConfig {
    bool enableAsyncConsole;           // 是否异步控制台输出
    size_t consoleBatchSize;           // 控制台小批次
    size_t fileBatchSize;              // 文件大批次
    milliseconds consoleFlushInterval; // 控制台刷新间隔
    milliseconds fileFlushInterval;    // 文件刷新间隔
    size_t consoleBufferSize_kb;       // 控制台预分配缓冲区大小
    size_t fileBufferSize_kb;          // 文件预分配缓冲区大小
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
    auto &helper = JsonHelper::GetInstance();
    { // basic config
      basic.maxConsoleLogQueueSize =
          helper.Get<int>("basic.maxConsoleLogQueueSize");
      basic.maxFileLogQueueSize = helper.Get<int>("basic.maxFileLogQueueSize");
    }

    { // output config
      output.logRootDir = helper.Get<string>("output.logRootDir");
      output.toFile = helper.Get<bool>("output.toFile");
      output.toConsole = helper.Get<bool>("output.toConsole");
      output.minLogLevel = helper.Get<int>("output.minLogLevel");
    }

    { // performance config
      performance.consoleBatchSize =
          helper.Get<int>("performance.consoleBatchSize");
      performance.fileBatchSize = helper.Get<int>("performance.fileBatchSize");
      performance.consoleFlushInterval =
          milliseconds(helper.Get<int>("performance.consoleFlushInterval_ms"));
      performance.fileFlushInterval =
          milliseconds(helper.Get<int>("performance.fileFlushInterval_ms"));
      performance.enableAsyncConsole =
          helper.Get<bool>("performance.enableAsyncConsole");
      performance.consoleBufferSize_kb =
          helper.Get<int>("performance.consoleBufferSize_kb");
      performance.fileBufferSize_kb =
          helper.Get<int>("performance.fileBufferSize_kb");
    }

    { // rotation config
      rotation.maxFileSize = helper.Get<int>("rotation.maxLogFileSize");
      rotation.maxFileCount = helper.Get<int>("rotation.maxLogFileCount");
    }

    { // cloud config
      cloud.enable = helper.Get<bool>("cloud.enable");
      cloud.serverUrl = helper.Get<string>("cloud.serverUrl");
      cloud.uploadEndpoint = helper.Get<string>("cloud.uploadEndpoint");
      cloud.uploadTimeout_s = helper.Get<int>("cloud.uploadTimeout_s");
      cloud.deleteAfterUpload = helper.Get<bool>("cloud.deleteAfterUpload");
      cloud.apiKey = helper.Get<string>("cloud.apiKey");
      cloud.maxRetries = helper.Get<int>("cloud.maxRetries");
      cloud.retryDelay_s = helper.Get<int>("cloud.retryDelay_s");
      cloud.maxQueueSize = helper.Get<int>("cloud.maxQueueSize");
    }
  }
};
} // namespace lyf