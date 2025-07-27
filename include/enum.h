#pragma once

namespace lyf {

enum class RotationType {
  BY_SIZE,         // 按文件大小轮转
  BY_TIME,         // 按时间轮转(每天)
  BY_SIZE_AND_TIME // 按大小和时间轮转
};

enum class LogLevel {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
  FATAL = 4,
};

} // namespace lyf