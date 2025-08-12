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
  for (auto &t : threads) {
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
  std::cout << "Current log file is at: "
            << lyf::AsyncLogSystem::GetInstance().getCurrentLogFilePath()
            << std::endl;

  // 程序退出时，日志系统将自动停止并清理。
  // 析构时会确保所有日志都被刷新和上传。
  return 0;
}