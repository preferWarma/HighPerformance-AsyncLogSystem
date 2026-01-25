#pragma once

#include "LogSink.h"
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lyf {

// Sink管理器,负责管理所有注册的Sink
class SinkManager {
public:
  void RegisterSinkFactory(const std::string &name,
                           std::unique_ptr<ILogSinkFactory> factory) {
    std::unique_lock lock(mtx_);
    factories_[name] = std::move(factory);
  }

  // 创建并添加Sink
  bool AddSink(std::string_view factoryName, const LogConfig &config) {
    std::unique_lock lock(mtx_);

    auto it = factories_.find(factoryName);
    if (it == factories_.end()) {
      return false;
    }

    auto sink = it->second->CreateSink();
    if (!sink->Initialize(config)) {
      return false;
    }

    sinks_.push_back(std::move(sink));
    return true;
  }

  // 直接添加已创建的Sink
  void AddSink(std::unique_ptr<ILogSink> sink) {
    std::unique_lock lock(mtx_);
    sinks_.push_back(std::move(sink));
  }

  // 写入单条消息到所有Sink
  void Write(const LogMessage &msg) {
    std::shared_lock lock(mtx_);
    for (auto &sink : sinks_) {
      sink->Write(msg);
    }
  }

  // 批量写入消息到所有Sink
  void WriteBatch(std::span<const LogMessage> messages) {
    std::shared_lock lock(mtx_);
    for (auto &sink : sinks_) {
      sink->WriteBatch(messages);
    }
  }

  // 刷新所有Sink
  void FlushAll() {
    std::shared_lock lock(mtx_);
    for (auto &sink : sinks_) {
      sink->Flush();
    }
  }

  // 关闭所有Sink
  void ShutdownAll() {
    std::unique_lock lock(mtx_);
    for (auto &sink : sinks_) {
      sink->Shutdown();
    }
    sinks_.clear();
  }

  // 获取所有Sink的名称
  std::vector<std::string> GetSinkNames() const {
    std::shared_lock lock(mtx_);
    std::vector<std::string> names;
    names.reserve(sinks_.size());
    for (const auto &sink : sinks_) {
      names.emplace_back(sink->GetName());
    }
    return names;
  }

  // 获取Sink数量
  size_t GetSinkCount() const {
    std::shared_lock lock(mtx_);
    return sinks_.size();
  }

  // 移除所有Sink
  void RemoveAllSinks() {
    std::unique_lock lock(mtx_);
    for (auto &sink : sinks_) {
      sink->Shutdown();
    }
    sinks_.clear();
  }

  // 根据名称查找Sink
  ILogSink *FindSink(std::string_view name) {
    std::shared_lock lock(mtx_);
    for (auto &sink : sinks_) {
      if (sink->GetName() == name) {
        return sink.get();
      }
    }
    return nullptr;
  }

private:
  mutable std::shared_mutex mtx_; // 读写锁
  std::vector<std::unique_ptr<ILogSink>> sinks_;
  std::map<std::string, std::unique_ptr<ILogSinkFactory>, std::less<>>
      factories_;
};

} // namespace lyf