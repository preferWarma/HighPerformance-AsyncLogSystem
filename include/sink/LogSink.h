#pragma once

#include "Config.h"
#include "LogQue.h"
#include <memory>
#include <span>
#include <string_view>

namespace lyf {

// 零拷贝日志输出接口基类
class ILogSink {
public:
  virtual ~ILogSink() = default;
  virtual bool Initialize(const LogConfig &config) = 0;
  virtual void Write(const LogMessage &msg) = 0;
  virtual void Flush() = 0;
  virtual void Shutdown() = 0;
  virtual std::string_view GetName() const = 0;

  virtual void WriteBatch(std::span<const LogMessage> messages) {
    for (const auto &msg : messages) {
      Write(msg);
    }
  }

  virtual bool SupportsAsync() const { return true; }
  virtual size_t GetRecommendedBatchSize() const { return 1024; }
};

// Sink工厂接口
class ILogSinkFactory {
public:
  virtual ~ILogSinkFactory() = default;
  virtual std::unique_ptr<ILogSink> CreateSink() = 0;
  virtual std::string_view GetSinkType() const = 0;
};

// 便捷的工厂模板
template <typename SinkType> class LogSinkFactory : public ILogSinkFactory {
public:
  std::unique_ptr<ILogSink> CreateSink() override {
    return std::make_unique<SinkType>();
  }

  std::string_view GetSinkType() const override {
    return SinkType::StaticGetName();
  }
};

} // namespace lyf