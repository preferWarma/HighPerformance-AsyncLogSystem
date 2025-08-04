# 高性能云存储异步日志系统

## 1. 项目简介

这是一个基于C++17的异步日志系统，能够将日志文件上传到云端存储。本项目旨在提供一个高性能、可靠的日志解决方案，适用于需要持久化和远程存储日志的各种应用场景。

## 2. 主要特性

*   **异步日志记录**：使用无锁并发队列和生产者-消费者模型，将日志写入与主线程分离，最大限度地减少对主业务逻辑的影响。
*   **多级日志**：支持 `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL` 5种日志级别，可配置日志级别过滤。
*   **日志文件轮转**：当日志文件达到一定大小或满足特定时间条件时，会自动创建新的日志文件。
*   **云端上传**：在日志文件轮转时，自动将旧的日志文件上传到指定的云存储服务。
*   **配置灵活**：通过 `config.json` 文件可以方便地配置日志级别、性能优化参数（如缓冲区大小、文件轮转策略）、云存储参数等。
*   **跨平台**：使用CMake构建，方便在不同操作系统上编译和运行。

## 3. 依赖

*   C++17
*   CMake
*   libcurl（可选，用于上传日志到云端，条件编译，可插拔，若需要启动上传功能，在Config.h中取消CLOUD_INCLUDE宏的注释）

## 4. 如何构建

```bash

# 创建构建目录
mkdir build
cd build

# 配置和构建(请在构建前配置一下config.json)
cmake ..
make
```

## 5. 如何运行

### 5.1. 配置

在运行前，请修改构建目录下的 `config.json` 文件，配置您的云存储信息（如URL、access key等）以及日志参数，以下是默认参数：

```json
{
    "basic": {
        "maxConsoleLogQueueSize": 1000,
        "maxFileLogQueueSize": 2000
    },
    "output": {
        "logRootDir": "./logs",
        "toFile": true,
        "toConsole": true,
        "minLogLevel": 0
    },
    "performance": {
        "enableAsyncConsole": true,
        "consoleFlushInterval_ms": 500,
        "fileFlushInterval_ms": 1000,
        "consoleBatchSize": 100,
        "fileBatchSize": 200
    },
    "rotation": {
        "maxLogFileSize": 10485760,
        "maxLogFileCount": 10
    },
    "cloud": {
        "enable": true,
        "serverUrl": "localhost:50000",
        "uploadEndpoint": "/api/upload",
        "uploadTimeout_s": 30,
        "deleteAfterUpload": false,
        "apiKey": "123456",
        "maxRetries": 3,
        "retryDelay_s": 5,
        "maxQueueSize": 100
    }
}
```

### 5.2. 运行example程序

```bash
./main
```

### 5.3. 运行测试

```bash
cd build/test
./log_system_test
```

### 5.4 压力测试

```bash
cd build/test
./log_benchmark --benchmark_out=./benchmark.json --benchmark_out_format=json
```

## 6. 使用示例
项目中cloud_server/app.py 是一个基于Flask的简单云存储服务器，用于接收日志上传请求。
可以按如下方式运行：

```bash
cd cloud_server
# 虚拟环境
python3 -m venv venv
source venv/bin/activate
# 安装依赖
pip3 install -r requirements.txt
# 初始化数据库
flask init-db
# 运行服务器
python3 app.py
```

```cpp
#include "LogSystem.h"

// 模拟多线程日志记录
void log_spam() {
    for (int i = 0; i < 100; ++i) {
        LOG_INFO("This is log message number {}", i);
    }
}

int main() {
    // 日志系统是单例，在第一次使用时会自动初始化。
    
    // 1. 基本日志记录，使用{}作为格式化占位符
    // 使用宏来记录不同严重级别的消息。
    LOG_INFO("Application starting up...");
    LOG_DEBUG("This is a debug message with a number: {}", 123);
    LOG_WARN("This is a warning. Something might be wrong.");
    LOG_ERROR("This is an error message. Action required. Error code: {}", 500);
    LOG_FATAL("This is a fatal error. The application will now terminate.");

    // 2. 多线程日志记录
    // 日志系统是线程安全的。
    std::cout << "\n--- Testing multi-threaded logging ---\n";
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(log_spam);
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "Multi-threaded logging test finished.\n";

    // 3. 刷新日志
    // 默认情况下，日志是异步写入的。
    // 您可以手动刷新缓冲区，以确保所有待处理的日志都已写入磁盘。
    std::cout << "\n--- Flushing logs ---\n";
    lyf::AsyncLogSystem::GetInstance().Flush();
    std::cout << "Logs flushed.\n";

    // 4. 日志文件轮转
    // 您可以手动触发日志文件轮转。
    // 发生轮转时，如果已配置，旧的日志文件可能会被上传到云端。
    std::cout << "\n--- Forcing log rotation ---\n";
    lyf::AsyncLogSystem::GetInstance().forceRotation();
    std::cout << "Log rotation forced.\n";
    
    LOG_INFO("This message will be in the new log file.");

    // 5. 获取当前日志文件路径
    std::cout << "\n--- Current log file ---\n";
    std::cout << "Current log file is at: " << lyf::AsyncLogSystem::GetInstance().getCurrentLogFilePath() << std::endl;

    // 程序退出时，日志系统将自动停止并清理。
    // 析构时会确保所有日志都被刷新和上传。
    return 0;
}
```