#include "Logger.h"
#include "sinks/ISink.h"
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <vector>

namespace {
class InMemorySink : public lyf::ILogSink {
public:
  void Log(const lyf::LogMessage &msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    formatter_.Format(msg, buffer_);
    records_.emplace_back(buffer_.begin(), buffer_.end());
  }

  void Flush() override {}
  void Sync() override {}

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
  }

  std::vector<std::string> Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
  }

private:
  mutable std::mutex mutex_;
  std::vector<std::string> records_;
};
} // namespace

class LoggerSuite : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    lyf::LogConfig cfg;
    cfg.SetLevel(lyf::LogLevel::INFO)
        .SetQueueCapacity(1024)
        .SetQueueFullPolicy(lyf::QueueFullPolicy::BLOCK)
        .SetBufferPoolSize(1024)
        .SetTLSBufferCount(8)
        .SetLogPath("");
    lyf::Logger::Instance().Init(cfg);
    sink_ = std::make_shared<InMemorySink>();
    lyf::Logger::Instance().AddSink(sink_);
  }

  static void TearDownTestSuite() {
    lyf::Logger::Instance().Shutdown();
    sink_.reset();
  }

  void SetUp() override { sink_->Clear(); }

  static std::shared_ptr<InMemorySink> sink_;
};

std::shared_ptr<InMemorySink> LoggerSuite::sink_;

TEST_F(LoggerSuite, FiltersByLevel) {
  DEBUG("debug {}", 1);
  INFO("info {}", 2);
  lyf::Logger::Instance().Sync();
  auto records = sink_->Snapshot();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_NE(records[0].find("INFO"), std::string::npos);
  EXPECT_NE(records[0].find("info 2"), std::string::npos);
  EXPECT_EQ(records[0].find("DEBUG"), std::string::npos);
}

TEST_F(LoggerSuite, RecordsMultipleMessages) {
  INFO("first");
  WARN("second");
  lyf::Logger::Instance().Sync();
  auto records = sink_->Snapshot();
  ASSERT_EQ(records.size(), 2u);
  EXPECT_NE(records[0].find("INFO"), std::string::npos);
  EXPECT_NE(records[1].find("WARN"), std::string::npos);
}

TEST_F(LoggerSuite, HandlesEmptyMessage) {
  INFO("");
  lyf::Logger::Instance().Sync();
  auto records = sink_->Snapshot();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_FALSE(records[0].empty());
  EXPECT_EQ(records[0].back(), '\n');
}
