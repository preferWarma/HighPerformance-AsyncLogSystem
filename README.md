# 高性能异步C++日志库

这是一个从零开始构建的、以极致性能为目标的C++异步日志库。在多项关键性能指标上，本系统均以数倍乃至数十倍的优势超越了业界广泛使用的开源日志库 `spdlog`。项目采用全head-only的设计，无需编译，直接引入`include`目录下的全部头文件即可使用。

## ✨ 核心特性

- **极致性能**: 在8核并发下，吞吐量高达 **1000万条/秒**，单次调用延迟低至 **57纳秒**。
- **高度异步**: 采用多生产者-单消费者的并发模型，日志调用对业务线程影响极小。
- **无锁设计**: 核心队列采用 `moodycamel::ConcurrentQueue` 无锁队列，根除了高并发下的锁竞争瓶颈。
- **零分配格式化**: 自定义的高性能格式化引擎，结合C++20编译期优化，实现日志生成时的零堆内存分配，在当前日志的需求场景下进行了特定优化和精简，格式化性能超过std::format和fmt::format。
- **I/O优化**: 内置批量处理与文件写入缓冲机制，降低系统调用开销。
- **功能完备**:
    - 支持多种日志级别 (DEBUG, INFO, WARN, ERROR, FATAL)。
    - 支持日志级别过滤，可以根据需要过滤不同级别的日志。
    - 支持文件自动轮转（按大小/按时间）。
    - 支持历史日志清理。
    - 支持JSON文件进行灵活配置，无需重新编译。
    - (可选) 支持将日志文件上传至云端服务器。
- **工程实践**:
    - 使用 **Google Test** 搭建了完整的单元/集成测试。
    - 使用 **Google Benchmark** 进行了系统化的性能剖析与迭代调优。

## 🚀 性能测试：vs. spdlog
在中小规模并发下（例如4线程内）扩展性近乎线性，更高并发下受单消费者模型限制，
在同等硬件与测试条件下，本日志系统与 `spdlog` 的核心性能指标对比如下：

| 对比维度        | **本日志系统 (YourLogger)** | spdlog (v1.10.0) | **结论**       |
| :-------------- | :-------------------------- | :--------------- | :------------- |
| **吞吐量 (1T)** | **~20.0 M/s**               | ~5.0 M/s         | **快 4.0 倍**  |
| **吞吐量 (2T)** | **~19.7 M/s**               | ~1.7 M/s         | **快 11.6 倍** |
| **吞吐量 (4T)** | **~19.4 M/s**               | ~1.9 M/s         | **快 10.2 倍** |
| **吞吐量 (8T)** | **~13.2 M/s**               | ~0.15 M/s        | **快 88.0 倍** |
| **P99 延迟**    | **~57 ns**                  | ~200 ns          | **低 71.5%**   |

*详细的压测数据与脚本请见 `test/` 目录。*

## ⚙️ 如何使用

### 依赖
项目采用head-only设计，单纯使用只需要拷贝`include`目录的头文件即可，不需要链接其他第三方库。
必备：
- C++17 或更高版本（C++20及以上版本有更好的性能）
- CMake 3.15 或更高版本
- `moodycamel::ConcurrentQueue` (用于日志队列，已包含在项目中)
- `nlohmann/json`（用于配置文件解析，已包含在项目中）

可选：
- `curl` (可选，条件编译，仅用于上传日志到云端)
- `fmt` (仅用于格式化性能对比测试)
- `spdlog` (仅用于性能对比测试)
- `gtest` (用于单元测试)
- `benchmark` (用于性能测试)

### 构建

```bash
# 1. 克隆仓库
git clone https://github.com/preferWarma/HighPerformance-AsyncLogSystem.git
cd HighPerformance-AsyncLogSystem

# 2. 配置并构建
cmake -B build
cmake --build build

# 3. 运行
# 运行单元测试
cd build/test
ctest

# 运行格式化性能测试
./build/test/format_comparison

# 运行本项目的性能测试
./build/test/comparison_mylogger

# 运行 spdlog 性能对比测试
./build/test/comparison_spdlog
```

### 在你的项目中使用

1.  **包含头文件**:
  ```cpp
  #include "LogSystem.h"
  ```
2.  **调用日志宏**:
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
      // 日志会根据配置自动轮转，也可以手动触发。
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
项目中`cloud_server/app.py` 是一个基于Flask的简单云存储服务器，用于接收日志上传请求。 可以按如下方式运行：
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
### 灵活配置

所有日志系统的行为都可以通过修改项目根目录下的 `config.json` 文件来灵活配置，无需重新编译代码。以下是默认配置项:

```jsonc
{
    "basic": {
        "maxConsoleLogQueueSize": 4096, // 控制台日志队列大小
        "maxFileLogQueueSize": 8192 // 文件日志队列大小
    },
    "output": {
        "logRootDir": "./logs", // 日志文件根目录
        "toFile": true, // 是否输出到文件
        "toConsole": true, // 是否输出到控制台
        "minLogLevel": 0, // 最小日志级别
    },
    "performance": {
        "enableAsyncConsole": true, // 是否异步输出到控制台
        "consoleFlushInterval_ms": 50, // 控制台刷新间隔（毫秒）
        "fileFlushInterval_ms": 100, // 文件刷新间隔（毫秒）
        "consoleBatchSize": 512, // 控制台批量处理大小
        "fileBatchSize": 1024, // 文件批量处理大小
        "consoleBufferSize_kb": 16, // 控制台缓冲区大小（KB）
        "fileBufferSize_kb": 32 // 文件缓冲区大小（KB）
    },
    "rotation": {
        "maxLogFileSize": 10485760, // 最大日志文件大小（字节）
        "maxLogFileCount": 10 // 最大日志文件数量
    },
    "cloud": {
        "enable": true, // 是否启用云端日志上传
        "serverUrl": "localhost:50000", // 云端服务器地址
        "uploadEndpoint": "/api/upload", // 上传日志的 API 端点
        "uploadTimeout_s": 30,  // 上传超时时间（秒）
        "deleteAfterUpload": false, // 是否上传后删除本地日志文件
        "apiKey": "123456", // 云端服务器 API 密钥
        "maxRetries": 3,  // 最大重试次数
        "retryDelay_s": 5,  // 重试延迟时间（秒）
        "maxQueueSize": 100 // 上传队列最大大小
    }
}
```