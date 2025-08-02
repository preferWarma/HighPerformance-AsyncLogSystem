#include "LogSystem.h"

int main() {
  // 初始化日志系统
  auto &logger = lyf::AsyncLogSystem::GetInstance();

  // 使用日志系统
  LOG_INFO("System started");
  LOG_DEBUG("Debug message: {}", 42);
  LOG_WARN("Warning: disk space low");
  LOG_ERROR("Error occurred: {}", "connection failed");

  // 等待1秒，确保日志被写入文件，否则轮转时队列的日志会写入到轮转后的新文件中
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // 手动触发轮转（会自动上传旧文件）
  logger.forceRotation();

  // 程序结束时自动清理
  return 0;
}