#include "LogConfig.h"
#include "LogMessage.h"
#include "Sink.h"
#include "tool/Utility.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

using namespace lyf;

class FileSinkRotateTest : public ::testing::Test {
protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() / "filesink_rotate_test";
    std::filesystem::create_directories(test_dir_);
    log_path_ = test_dir_ / "test.log";
    buffer_pool_ = std::make_unique<BufferPool>(100);
  }

  void TearDown() override {
    buffer_pool_.reset();
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  LogMessage CreateLogMessage(std::string_view content) {
    LogBuffer *buf = buffer_pool_->Alloc();
    // 将内容复制到 buffer
    size_t copy_len = std::min(content.size(), LogBuffer::SIZE - 1);
    std::memcpy(buf->data, content.data(), copy_len);
    buf->length = copy_len;
    buf->data[copy_len] = '\0';
    return LogMessage(LogLevel::INFO, "test.cpp", 1, std::this_thread::get_id(),
                      buf, buffer_pool_.get());
  }

  size_t GetFileCount() {
    size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(test_dir_)) {
      if (entry.is_regular_file()) {
        ++count;
      }
    }
    return count;
  }

  size_t GetRotateFileCount() {
    size_t count = 0;
    std::string base_name = log_path_.filename().string();
    for (const auto &entry : std::filesystem::directory_iterator(test_dir_)) {
      if (entry.is_regular_file()) {
        std::string filename = entry.path().filename().string();
        if (filename != base_name && starts_with(filename, base_name)) {
          ++count;
        }
      }
    }
    return count;
  }

  std::filesystem::path test_dir_;
  std::filesystem::path log_path_;
  std::unique_ptr<BufferPool> buffer_pool_;
};

// 测试：默认不轮转
TEST_F(FileSinkRotateTest, NoRotateByDefault) {
  {
    FileSink sink(log_path_.string());

    // 写入一些日志
    for (int i = 0; i < 10; ++i) {
      auto msg = CreateLogMessage("Test log message " + std::to_string(i));
      sink.Log(msg);
    }
    sink.Flush();
  }

  // 应该只有一个文件
  EXPECT_EQ(GetFileCount(), 1);
  EXPECT_TRUE(std::filesystem::exists(log_path_));
}

// 测试：轮转策略字符串解析
TEST(RotatePolicyTest, ParseRotatePolicy) {
  EXPECT_EQ(ParseRotatePolicy("NONE"), RotatePolicy::NONE);
  EXPECT_EQ(ParseRotatePolicy("DAILY"), RotatePolicy::DAILY);
  EXPECT_EQ(ParseRotatePolicy("SIZE"), RotatePolicy::SIZE);

  // 无效值应该返回 NONE
  EXPECT_EQ(ParseRotatePolicy("INVALID"), RotatePolicy::NONE);
  EXPECT_EQ(ParseRotatePolicy(""), RotatePolicy::NONE);
}

// 测试：轮转策略字符串转换
TEST(RotatePolicyTest, RotatePolicyToString) {
  EXPECT_EQ(RotatePolicyToString(RotatePolicy::NONE), "NONE");
  EXPECT_EQ(RotatePolicyToString(RotatePolicy::DAILY), "DAILY");
  EXPECT_EQ(RotatePolicyToString(RotatePolicy::SIZE), "SIZE");
}

// 测试：LogConfig 轮转配置
TEST_F(FileSinkRotateTest, LogConfigRotateSettings) {
  LogConfig config;

  // 测试默认配置
  EXPECT_EQ(config.GetRotatePolicy(), RotatePolicy::NONE);
  EXPECT_EQ(config.GetRotateSizeMB(), LogConfig::kDefaultRotateSizeMB);
  EXPECT_EQ(config.GetMaxRotateFiles(), LogConfig::kDefaultMaxRotateFiles);

  // 测试设置配置
  config.SetRotatePolicy(RotatePolicy::SIZE);
  config.SetRotateSizeMB(50);
  config.SetMaxRotateFiles(3);

  EXPECT_EQ(config.GetRotatePolicy(), RotatePolicy::SIZE);
  EXPECT_EQ(config.GetRotateSizeMB(), 50);
  EXPECT_EQ(config.GetMaxRotateFiles(), 3);
}

// 测试：从配置文件加载轮转配置
TEST_F(FileSinkRotateTest, LoadRotateConfigFromFile) {
  // 创建临时配置文件
  auto config_path = test_dir_ / "rotate_config.toml";
  std::ofstream ofs(config_path);
  ofs << R"([sink.file]
rotate_policy = "SIZE"
rotate_size_mb = 50
max_rotate_files = 3
)";
  ofs.close();

  LogConfig config;
  bool loaded = config.LoadFromFile(config_path.string());
  EXPECT_TRUE(loaded);

  EXPECT_EQ(config.GetRotatePolicy(), RotatePolicy::SIZE);
  EXPECT_EQ(config.GetRotateSizeMB(), 50);
  EXPECT_EQ(config.GetMaxRotateFiles(), 3);
}

// 测试：Size 轮转策略
TEST_F(FileSinkRotateTest, SizeRotatePolicy) {
  {
    LogConfig config;
    config.SetRotatePolicy(RotatePolicy::SIZE);
    config.SetRotateSizeMB(1); // 1MB
    config.SetMaxRotateFiles(3);

    FileSink sink(log_path_.string(), config);
    std::string content(3000, 'A'); // 约 3KB 每消息
    for (int i = 0; i < 500; ++i) { // 写入约 1.5MB
      auto msg = CreateLogMessage(content + std::to_string(i));
      sink.Log(msg);
    }
    sink.Flush();
  }

  // 应该有多个文件（原文件 + 轮转文件）
  EXPECT_GE(GetFileCount(), 2);
}

// 测试：轮转文件数量限制
TEST_F(FileSinkRotateTest, MaxRotateFilesLimit) {
  const size_t max_files = 2;

  {
    LogConfig config;
    config.SetRotatePolicy(RotatePolicy::SIZE);
    config.SetRotateSizeMB(1); // 1MB
    config.SetMaxRotateFiles(max_files);

    FileSink sink(log_path_.string(), config);
    std::string content(3000, 'B');           // 约 3KB 每消息
    for (int round = 0; round < 5; ++round) { // 多轮写入，每轮约 1.5MB
      for (int i = 0; i < 500; ++i) {
        auto msg = CreateLogMessage(content + std::to_string(round) + "_" +
                                    std::to_string(i));
        sink.Log(msg);
      }
    }
    sink.Flush();
  }

  // 轮转文件数量应该不超过限制
  EXPECT_LE(GetRotateFileCount(), max_files);
}

// 测试：ApplyConfig 应用轮转配置
TEST_F(FileSinkRotateTest, ApplyConfig) {
  LogConfig config;
  config.SetRotatePolicy(RotatePolicy::SIZE);
  config.SetRotateSizeMB(10);
  config.SetMaxRotateFiles(5);

  FileSink sink(log_path_.string());
  sink.ApplyConfig(config);

  // 写入数据验证配置已应用
  std::string content(100, 'C');
  for (int i = 0; i < 5; ++i) {
    auto msg = CreateLogMessage(content);
    sink.Log(msg);
  }
  sink.Flush();

  // 文件应该存在
  EXPECT_TRUE(std::filesystem::exists(log_path_));
}

// 测试：文件写入和轮转后内容正确性
TEST_F(FileSinkRotateTest, FileContentIntegrity) {
  std::vector<std::string> messages;
  for (int i = 0; i < 5; ++i) {
    messages.push_back("Message_" + std::to_string(i));
  }

  {
    FileSink sink(log_path_.string());
    for (const auto &msg : messages) {
      auto log_msg = CreateLogMessage(msg);
      sink.Log(log_msg);
    }
    sink.Flush();
  }

  // 读取文件内容验证
  std::ifstream ifs(log_path_);
  std::string content((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());

  // 验证所有消息都在文件中
  for (const auto &msg : messages) {
    EXPECT_NE(content.find(msg), std::string::npos);
  }
}
