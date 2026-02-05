#include "Logger.h"
#include "tool/Timer.h"
#include <fstream>
#include <iostream>
#include <sys/fcntl.h>
#include <unistd.h>

using namespace lyf;
constexpr int kLogCount = 1e8;
constexpr const char *kLogFile = "app.log";

void truncateLogFile(const std::string &logfile) {
  std::fstream ofs(logfile, std::ios::out | std::ios::trunc);
  ofs.close();
}

void init() {
  QueConfig cfg(8192, QueueFullPolicy::BLOCK);
  auto &logger = Logger::Instance();
  // 初始化系统
  logger.Init(cfg, 8192);
  logger.SetLevel(LogLevel::DEBUG);

  // 添加 Sinks
  // logger.AddSink(std::make_shared<ConsoleSink>());
  logger.AddSink(std::make_shared<FileSink>(kLogFile));
}

size_t CountLines(const std::string &filename) {
  std::string cmd = "wc -l < " + filename;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return 0;

  size_t count = 0;
  fscanf(pipe, "%zu", &count);
  pclose(pipe);

  return count;
}

int main() {
  init();
  std::cout << "logfile: " << kLogFile << std::endl;
  truncateLogFile(kLogFile);

  // 计时器
  stopwatch sw_total(stopwatch::TimeType::s);
  stopwatch sw_log(stopwatch::TimeType::ns);
  stopwatch sw_flush(stopwatch::TimeType::s);
  sw_total.start();

  sw_log.start();
  // 写入kLogCount条日志
  for (int i = 0; i < kLogCount; ++i) {
    INFO("Hello, LogSystem! {}", i);
  }
  sw_log.stop();

  sw_flush.start();
  // 将所有日志刷到文件
  sw_flush.stop();
  Logger::Instance().Sync();
  sw_total.stop();

  auto line_count = CountLines(kLogFile);
  if (line_count != kLogCount) {
    std::cerr << "Error: logfile is incomplete, expected " << kLogCount
              << " lines, but got " << line_count << " lines." << std::endl;
    std::cout << "drop count: " << Logger::Instance().GetDropCount()
              << std::endl;
  }

  // 计算性能指标
  std::cout << "total time: " << sw_total.duration() << " s" << std::endl;
  std::cout << "avg per log: " << sw_log.duration() / kLogCount << " ns"
            << std::endl;
  std::cout << "Flush time: " << sw_flush.duration() << " s" << std::endl;
  auto logfile_size_MB = std::filesystem::file_size(kLogFile) / (1024 * 1024);
  std::cout << "logfile size: " << logfile_size_MB << " MB" << std::endl;
  std::cout << "avg throughput: " << 1.0 * logfile_size_MB / sw_total.duration()
            << " MB/s" << std::endl;

  return 0;
}