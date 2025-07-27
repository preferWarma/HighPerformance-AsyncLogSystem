#pragma once

#include <chrono>
#include <iomanip>
#include <string>
#include <thread>

namespace lyf {

using std::string, std::thread;
namespace chrono = std::chrono;
using system_clock = chrono::system_clock;

/// @brief 获取当前时间戳
/// @return 当前时间戳
/// @note 单位为毫秒
inline int64_t getCurrentTimeStamp() {
  return duration_cast<chrono::milliseconds>(
             system_clock::now().time_since_epoch())
      .count();
}

/// @brief 获取当前时间的格式化字符串
/// @param format 时间格式字符串, 默认为"%Y-%m-%d %H:%M:%S"
/// @return 格式化后的时间字符串
inline string getCurrentTime(const string &format = "%Y-%m-%d %H:%M:%S") {
  auto now = system_clock::now();
  auto time_t = system_clock::to_time_t(now);
  std::stringstream ss;
  // 线程安全的时间格式化函数
  ss << std::put_time(std::localtime(&time_t), format.c_str());
  return ss.str();
}

/// @brief 格式化时间点为字符串
/// @param timePoint 时间点
/// @param format 时间格式字符串, 默认为"%Y-%m-%d %H:%M:%S"
/// @return 格式化后的时间字符串
inline string formatTime(const system_clock::time_point &timePoint,
                         const string &format = "%Y-%m-%d %H:%M:%S") {
  std::time_t time = system_clock::to_time_t(timePoint);
  char buf[1024];
  strftime(buf, sizeof(buf), format.c_str(), localtime(&time));
  return buf;
}

/// @brief 辅助函数, 将单个参数转化为字符串
template <typename T> string to_string(T &&arg) {
  std::stringstream oss;
  oss << std::forward<T>(arg);
  return oss.str();
}

/// @brief 使用模板折叠格式化日志消息，支持 "{}" 占位符
/// @param fmt 格式字符串
/// @param args 参数值
template <typename... Args>
string FormatMessage(const string &fmt, Args &&...args) {
  if constexpr (sizeof...(args) == 0) {
    return fmt; // 没有参数时直接返回格式字符串
  }
  std::ostringstream oss;
  size_t argIndex = 0;
  size_t pos = 0;

  auto process_arg = [&](auto &&arg) {
    size_t placeholder = fmt.find("{}", pos);
    if (placeholder != string::npos) {
      oss << fmt.substr(pos, placeholder - pos);
      oss << std::forward<decltype(arg)>(arg);
      pos = placeholder + 2;
    } else {
      // 没有更多占位符，直接追加参数
      oss << std::forward<decltype(arg)>(arg);
    }
  };

  (process_arg(std::forward<Args>(args)), ...);

  // 添加剩余的格式字符串
  oss << fmt.substr(pos);
  return oss.str();
}

/// @brief RAII 确保标志被重置
class FlagGuard {
public:
  FlagGuard(std::atomic<bool> &flag) : _flag(flag) {}
  ~FlagGuard() { _flag.store(false); }

private:
  std::atomic<bool> &_flag;
};

} // namespace lyf
