// #define LYF_INNER_LOG

#include "Config.h"
#include "LogSystem.h"
#include "third/toml.hpp"
#include "tool/Timer.h"
#include <fstream>
#include <iostream>

using namespace lyf;
constexpr int kLogCount = 1e7;

void truncateLogFile(const std::string &logfile) {
  std::fstream ofs(logfile, std::ios::out | std::ios::trunc);
  ofs.close();
}

int main() {
  INIT_LOG_SYSTEM("config.toml");
  std::cout << AsyncLogSystem::Instance().getConfig().data() << std::endl;

  // 清空当前日志文件
  auto logfile = AsyncLogSystem::Instance().getCurrentLogFilePath();
  std::cout << "logfile: " << logfile << std::endl;
  truncateLogFile(logfile);
  // 计时器
  stopwatch sw_total(stopwatch::TimeType::s);
  stopwatch sw_log(stopwatch::TimeType::us);
  stopwatch sw_flush(stopwatch::TimeType::s);
  sw_total.start();

  sw_log.start();
  // 写入kLogCount条日志
  for (int i = 0; i < kLogCount; ++i) {
    LOG_INFO("Hello, LogSystem! {}", i);
  }
  sw_log.stop();

  sw_flush.start();
  // 将所有日志刷到文件
  AsyncLogSystem::Instance().Flush();
  sw_flush.stop();

  sw_total.stop();
  // 计算性能指标
  std::cout << "avg per log: " << sw_log.duration() / kLogCount << " us"
            << std::endl;
  std::cout << "Flush time: " << sw_flush.duration() << " s" << std::endl;
  auto logfile_size_MB = std::filesystem::file_size(logfile) / (1024 * 1024);
  std::cout << "logfile size: " << logfile_size_MB << " MB" << std::endl;
  std::cout << "avg throughput: " << 1.0 * logfile_size_MB / sw_total.duration()
            << " MB/s" << std::endl;
}