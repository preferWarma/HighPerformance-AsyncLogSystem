#pragma once
#include "ConfigManager.h"
#include "Singleton.h"
#include <string>

namespace lyf {
using std::string;
using std::chrono::milliseconds;

struct Config : Singleton<Config> {
  friend class Singleton<Config>;

  struct BasicConfig {
    size_t maxConsoleLogQueueSize;
    size_t maxFileLogQueueSize;
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
  }
};
} // namespace lyf