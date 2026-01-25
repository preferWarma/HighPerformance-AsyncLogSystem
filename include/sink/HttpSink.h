#pragma once

#include "LogSink.h"
#include "third/httplib.h"
#include "third/nlohmann_json.h"
#include "tool/FastFormater.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lyf {

using json = nlohmann::json;

class HttpSink : public ILogSink {
public:
  static constexpr std::string_view StaticGetName() { return "HttpSink"; }

  struct HttpConfig {
    std::string url;
    std::string endpoint = "/logs";
    std::string contentType = "application/json";
    std::vector<std::pair<std::string, std::string>> headers;
    int timeout_sec = 30;
    int maxRetries = 3;
    size_t batchSize = 100;
    size_t bufferSize_kb = 64;
    bool enableCompression = false;
    bool enableAsync = true;
  };

  HttpSink() = default;
  ~HttpSink() override { Shutdown(); }

  bool Initialize(const LogConfig &config) override {
    logConfig_ = &config;
    buffer_.reserve(1024 * httpConfig_.bufferSize_kb);
    bufferThreshold_ = httpConfig_.bufferSize_kb * 1024 / 2;
    lastFlushTime_ = std::chrono::steady_clock::now();

    if (!InitializeHttpClient()) {
      return false;
    }

    initialized_ = true;
    return true;
  }

  void Write(const LogMessage &msg) override {
    std::lock_guard<std::mutex> lock(write_mtx_);

    if (!initialized_) {
      return;
    }

    pendingMessages_.push_back(msg);

    if (pendingMessages_.size() >= httpConfig_.batchSize) {
      FlushBuffer();
    }
  }

  void WriteBatch(std::span<const LogMessage> messages) override {
    if (messages.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(write_mtx_);

    if (!initialized_) {
      return;
    }

    pendingMessages_.insert(pendingMessages_.end(), messages.begin(),
                            messages.end());
    FlushBuffer();
  }

  void Flush() override {
    std::lock_guard<std::mutex> lock(write_mtx_);
    FlushBuffer();
  }

  void Shutdown() override {
    std::lock_guard<std::mutex> lock(write_mtx_);
    FlushBuffer();
    initialized_ = false;
    httpClient_.reset();
  }

  std::string_view GetName() const override { return StaticGetName(); }
  bool SupportsAsync() const override { return httpConfig_.enableAsync; }
  size_t GetRecommendedBatchSize() const override {
    return httpConfig_.batchSize;
  }

  void SetHttpConfig(const HttpConfig &config) { httpConfig_ = config; }
  const HttpConfig &GetHttpConfig() const { return httpConfig_; }

private:
  bool InitializeHttpClient() {
    try {
      if (httpConfig_.url.empty()) {
        HandleSendError("HTTP URL is not configured");
        return false;
      }

      httpClient_ = std::make_unique<httplib::Client>(httpConfig_.url.c_str());
      httpClient_->set_connection_timeout(httpConfig_.timeout_sec, 0);
      httpClient_->set_read_timeout(httpConfig_.timeout_sec, 0);
      httpClient_->set_write_timeout(httpConfig_.timeout_sec, 0);

      if (httpConfig_.enableCompression) {
        httpClient_->set_decompress(false);
      }

      return true;
    } catch (const std::exception &e) {
      HandleSendError(std::string("Failed to initialize HTTP client: ") +
                      e.what());
      return false;
    }
  }

  bool SendLogData(const std::string &jsonData) {
    if (!httpClient_) {
      HandleSendError("HTTP client is not initialized");
      return false;
    }

    int attempt = 0;
    while (attempt <= httpConfig_.maxRetries) {
      try {
        httplib::Headers headers;
        headers.insert({"Content-Type", httpConfig_.contentType});

        for (const auto &header : httpConfig_.headers) {
          headers.insert({header.first, header.second});
        }

        std::string fullEndpoint = httpConfig_.endpoint;
        if (fullEndpoint.empty()) {
          fullEndpoint = "/logs";
        }

        auto result = httpClient_->Post(fullEndpoint, headers, jsonData,
                                        httpConfig_.contentType);

        if (result && result->status == 200) {
          failedAttempts_ = 0;
          return true;
        } else {
          std::string errorMsg = result
                                     ? "HTTP request failed with status: " +
                                           std::to_string(result->status)
                                     : "HTTP request failed with no response";
          HandleSendError(errorMsg);
        }
      } catch (const std::exception &e) {
        HandleSendError(std::string("HTTP request exception: ") + e.what());
      }

      attempt++;
      if (ShouldRetry(attempt)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
      } else {
        break;
      }
    }

    failedAttempts_++;
    return false;
  }

  std::string FormatMessageToJson(const LogMessage &msg) {
    json log_entry;
    log_entry["timestamp"] = FormatTime(msg.time);
    log_entry["level"] = LevelToString(msg.level);
    log_entry["thread_id"] = msg.hash_tid;
    log_entry["file"] = msg.file_name;
    log_entry["line"] = msg.file_line;
    log_entry["content"] = msg.content;
    return log_entry.dump();
  }

  std::string FormatBatchToJson(std::span<const LogMessage> messages) {
    json log_batch;
    log_batch["logs"] = json::array();

    for (const auto &msg : messages) {
      json log_entry;
      log_entry["timestamp"] = FormatTime(msg.time);
      log_entry["level"] = LevelToString(msg.level);
      log_entry["thread_id"] = msg.hash_tid;
      log_entry["file"] = msg.file_name;
      log_entry["line"] = msg.file_line;
      log_entry["content"] = msg.content;
      log_batch["logs"].push_back(std::move(log_entry));
    }

    return log_batch.dump();
  }

  void FlushBuffer() {
    if (pendingMessages_.empty()) {
      return;
    }

    std::string jsonData = FormatBatchToJson(pendingMessages_);
    bool success = SendLogData(jsonData);

    if (success) {
      pendingMessages_.clear();
    } else if (pendingMessages_.size() > httpConfig_.batchSize * 2) {
      pendingMessages_.erase(pendingMessages_.begin(),
                             pendingMessages_.begin() + httpConfig_.batchSize);
    }

    lastFlushTime_ = std::chrono::steady_clock::now();
  }

  void HandleSendError(const std::string &error) {
    std::cerr << "[HttpSink] Error: " << error << std::endl;
  }

  bool ShouldRetry(int attempt) const {
    return attempt <= httpConfig_.maxRetries;
  }

  template <typename T> void FastAppend(std::string &buf, T value) {
    detail::FastAppender<T>::append(buf, std::forward<T>(value));
  }

private:
  const LogConfig *logConfig_ = nullptr;
  HttpConfig httpConfig_;
  std::unique_ptr<httplib::Client> httpClient_;
  std::mutex write_mtx_;
  std::string buffer_;
  std::vector<LogMessage> pendingMessages_;
  size_t bufferThreshold_ = 32 * 1024;
  std::chrono::steady_clock::time_point lastFlushTime_;
  int failedAttempts_ = 0;
  bool initialized_ = false;
};

} // namespace lyf
