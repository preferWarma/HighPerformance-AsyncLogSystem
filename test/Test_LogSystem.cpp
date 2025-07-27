#include "Config.h"
#include "LogSystem.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

using namespace testing;
using namespace lyf;

auto &config = Config::GetInstance();

class LogTestUtils {
public:
  static void SetupTestEnvironment(const std::string &testDir,
                                   const std::string &logMode = "FILE",
                                   const std::string &logLevel = "0") {
    config.output.logRootDir = testDir;
    config.output.toConsole = logMode.find("CONSOLE") != std::string::npos;
    config.output.toFile = logMode.find("FILE") != std::string::npos;
    config.output.minLogLevel = std::stoi(logLevel);

    CleanupTestDir(testDir);
    CreateTestDir(testDir);
  }

  static void CleanupTestEnvironment() { config.Init(); }

  static void CleanupTestDir(const std::string &dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }

  static bool CreateTestDir(const std::string &dir) {
    std::error_code ec;
    return std::filesystem::create_directories(dir, ec);
  }

  static size_t GetFileSize(const std::string &path) {
    std::error_code ec;
    return std::filesystem::file_size(path, ec);
  }

  static std::string ReadFile(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
      return "";
    }

    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
  }

  static size_t CountLines(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
      return 0;
    }

    size_t lines = 0;
    std::string line;
    while (std::getline(file, line)) {
      ++lines;
    }
    return lines;
  }

  static std::vector<std::string> GetLogFiles(const std::string &dir) {
    std::vector<std::string> files;
    std::error_code ec;

    if (!std::filesystem::exists(dir, ec)) {
      return files;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (entry.is_regular_file()) {
        std::string filename = entry.path().filename().string();
        if (filename.starts_with("log_") && filename.ends_with(".txt")) {
          files.push_back(entry.path().string());
        }
      }
    }

    std::sort(files.begin(), files.end());
    return files;
  }

  static void WaitForLogProcessing(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    std::this_thread::sleep_for(timeout);
  }
};

// 测试夹具基类
class LogSystemTestBase : public Test {
protected:
  void SetUp() override {
    test_dir_ =
        "test_logs_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    LogTestUtils::SetupTestEnvironment(test_dir_);
    std::cout << "test_dir_: " << test_dir_ << std::endl;
  }

  void TearDown() override {
    LogTestUtils::CleanupTestDir(test_dir_);
    LogTestUtils::CleanupTestEnvironment();
  }

  std::string test_dir_;
};

// ================================
// 基础功能测试
// ================================

class BasicFunctionalityTest : public LogSystemTestBase {};

TEST_F(BasicFunctionalityTest, BasicLoggingWorks) {
  LogTestUtils::SetupTestEnvironment(test_dir_, "FILE");
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();

  // 记录各种级别的日志
  LOG_DEBUG("Debug message: {}", 1);
  LOG_INFO("Info message: {}", 2);
  LOG_WARN("Warning message: {}", 3);
  LOG_ERROR("Error message: {}", 4);
  LOG_FATAL("Fatal message: {}", 5);

  logger.Stop();
  LogTestUtils::WaitForLogProcessing();

  // 验证日志文件存在
  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  std::cout << "test_dir_: " << test_dir_ << std::endl;
  std::cout << "env: LOG_FILE_DIR: " << config.output.logRootDir << std::endl;
  ASSERT_GE(logFiles.size(), 1) << "No log files created";

  // 验证日志内容
  std::string content = LogTestUtils::ReadFile(logFiles[0]);
  EXPECT_THAT(content, HasSubstr("Debug message: 1"));
  EXPECT_THAT(content, HasSubstr("Info message: 2"));
  EXPECT_THAT(content, HasSubstr("Warning message: 3"));
  EXPECT_THAT(content, HasSubstr("Error message: 4"));
  EXPECT_THAT(content, HasSubstr("Fatal message: 5"));

  // 验证日志级别格式
  EXPECT_THAT(content, HasSubstr("[DEBUG]"));
  EXPECT_THAT(content, HasSubstr("[INFO ]"));
  EXPECT_THAT(content, HasSubstr("[WARN ]"));
  EXPECT_THAT(content, HasSubstr("[ERROR]"));
  EXPECT_THAT(content, HasSubstr("[FATAL]"));
}

TEST_F(BasicFunctionalityTest, LogLevelFiltering) {
  // 设置最低日志级别为WARN (2)
  LogTestUtils::SetupTestEnvironment(test_dir_, "FILE", "2");

  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();

  LOG_DEBUG("This should not appear"); // Level 0 - filtered
  LOG_INFO("This should not appear");  // Level 1 - filtered
  LOG_WARN("This should appear");      // Level 2 - should appear
  LOG_ERROR("This should appear");     // Level 3 - should appear
  LOG_FATAL("This should appear");     // Level 4 - should appear

  logger.Stop();
  LogTestUtils::WaitForLogProcessing();

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  ASSERT_GE(logFiles.size(), 1);

  std::string content = LogTestUtils::ReadFile(logFiles[0]);

  // 应该包含WARN以上级别
  EXPECT_THAT(content, HasSubstr("This should appear"));

  // 不应该包含DEBUG和INFO级别
  EXPECT_THAT(content, Not(HasSubstr("This should not appear")));
}

TEST_F(BasicFunctionalityTest, DualOutputMode) {
  LogTestUtils::SetupTestEnvironment(test_dir_, "CONSOLE | FILE");

  // 重定向cout来捕获控制台输出
  std::ostringstream captured_output;
  std::streambuf *orig = std::cout.rdbuf();
  std::cout.rdbuf(captured_output.rdbuf());

  {
    auto &logger = AsyncLogSystem::GetInstance();
    logger.Init();

    LOG_INFO("Test dual output: {}", 42);

    logger.Stop();
    LogTestUtils::WaitForLogProcessing();
  }

  // 恢复cout
  std::cout.rdbuf(orig);

  // 验证文件输出
  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  ASSERT_GE(logFiles.size(), 1);

  std::string fileContent = LogTestUtils::ReadFile(logFiles[0]);
  EXPECT_THAT(fileContent, HasSubstr("Test dual output: 42"));

  // 验证控制台输出
  std::string consoleContent = captured_output.str();
  EXPECT_THAT(consoleContent, HasSubstr("Test dual output: 42"));
}

// ================================
// 日志轮转测试
// ================================

class LogRotationTest : public LogSystemTestBase {
protected:
  void SetUp() override {
    LogSystemTestBase::SetUp();
    config.rotation.maxFileSize = 1024; // 1KB
    config.rotation.maxFileCount = 3;
  }
};

TEST_F(LogRotationTest, SizeBasedRotation) {
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  logger.setRotationType(RotationType::BY_SIZE);
  logger.setMaxFileSize(1024); // 1KB
  logger.setMaxFileCount(3);

  // 写入大量日志触发轮转
  for (int i = 0; i < 100; ++i) {
    LOG_INFO("Size rotation test message {} - adding extra content to increase "
             "size and trigger rotation",
             i);
  }

  logger.Stop();
  LogTestUtils::WaitForLogProcessing(std::chrono::milliseconds(1000));

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);

  // 应该生成多个文件（轮转发生）
  EXPECT_GE(logFiles.size(), 2)
      << "Log rotation should have created multiple files";
  EXPECT_LE(logFiles.size(), 3) << "Should not exceed max file count";

  // 验证每个文件的大小
  for (const auto &file : logFiles) {
    size_t fileSize = LogTestUtils::GetFileSize(file);
    // 除了最后一个文件，其他文件应该接近最大大小
    if (file != logFiles.back()) {
      EXPECT_GE(fileSize, 800)
          << "Rotated file should be reasonably sized: " << file;
    }
  }
}

TEST_F(LogRotationTest, TimeBasedRotation) {
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  logger.setRotationType(RotationType::BY_TIME);

  // 模拟时间变化（这里简化为手动触发）
  LOG_INFO("Before rotation");

  // 手动触发轮转
  logger.forceRotation();

  LOG_INFO("After rotation");

  logger.Stop();
  LogTestUtils::WaitForLogProcessing();

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  EXPECT_GE(logFiles.size(), 2)
      << "Manual rotation should create multiple files";
}

TEST_F(LogRotationTest, FileCleanup) {
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  logger.setRotationType(RotationType::BY_SIZE);
  logger.setMaxFileSize(512); // 512B for quick rotation
  logger.setMaxFileCount(2);  // Only keep 2 files

  // 生成多个轮转文件
  for (int i = 0; i < 200; ++i) {
    LOG_INFO("Cleanup test message {} with extra content to force multiple "
             "rotations",
             i);
    if (i % 50 == 0) {
      LogTestUtils::WaitForLogProcessing(std::chrono::milliseconds(50));
    }
  }

  logger.Stop();
  LogTestUtils::WaitForLogProcessing(std::chrono::milliseconds(1000));

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  EXPECT_LE(logFiles.size(), 2)
      << "Should cleanup old files and keep only " << 2 << " files";
}

// ================================
// 线程安全测试
// ================================

class ThreadSafetyTest : public LogSystemTestBase {
protected:
  void SetUp() override {
    LogSystemTestBase::SetUp();
    LogTestUtils::SetupTestEnvironment(test_dir_, "FILE", "1");
  }
};

TEST_F(ThreadSafetyTest, ConcurrentLogging) {
  const int num_threads = 8;
  const int messages_per_thread = 500;
  std::atomic<int> total_messages{0};

  LogTestUtils::SetupTestEnvironment(test_dir_, "FILE", "0");
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();

  std::vector<std::future<void>> futures;
  auto start_time = std::chrono::high_resolution_clock::now();

  // 启动多个线程并发写日志
  for (int t = 0; t < num_threads; ++t) {
    futures.push_back(std::async(
        std::launch::async, [t, messages_per_thread, &total_messages]() {
          std::random_device rd;
          std::mt19937 gen(rd());
          std::uniform_int_distribution<> dis(1, 1000);

          for (int i = 0; i < messages_per_thread; ++i) {
            switch (i % 4) {
            case 0:
              LOG_INFO("Thread {} message {} - data: {}", t, i, dis(gen));
              break;
            case 1:
              LOG_WARN("Thread {} warning {} - data: {}", t, i, dis(gen));
              break;
            case 2:
              LOG_ERROR("Thread {} error {} - data: {}", t, i, dis(gen));
              break;
            case 3:
              LOG_DEBUG("Thread {} debug {} - data: {}", t, i, dis(gen));
              break;
            }
            total_messages.fetch_add(1);

            // 偶尔暂停模拟真实场景
            if (i % 100 == 0) {
              std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
          }
        }));
  }

  // 等待所有线程完成
  for (auto &future : futures) {
    future.wait();
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  logger.Stop();
  LogTestUtils::WaitForLogProcessing(std::chrono::milliseconds(1000));

  // 验证结果
  EXPECT_EQ(total_messages.load(), num_threads * messages_per_thread);

  // 检查日志文件完整性
  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  EXPECT_GE(logFiles.size(), 1);

  size_t total_log_lines = 0;
  for (const auto &file : logFiles) {
    total_log_lines += LogTestUtils::CountLines(file);
  }

  // 允许一些系统消息的误差
  int expected_messages = num_threads * messages_per_thread;
  EXPECT_GE(total_log_lines, expected_messages)
      << "Some log messages may have been lost";
  EXPECT_LE(total_log_lines, expected_messages + 20)
      << "Too many extra system messages";

  std::cout << "Concurrent logging: " << expected_messages << " messages in "
            << duration.count() << "ms ("
            << (expected_messages * 1000 / (duration.count() + 1))
            << " msgs/sec)" << std::endl;
}

TEST_F(ThreadSafetyTest, ConcurrentRotation) {
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  logger.setMaxFileSize(2048); // 2KB
  logger.setMaxFileCount(5);

  const int num_threads = 4;
  const int messages_per_thread = 200;

  std::vector<std::future<void>> futures;

  // 多个线程同时写入，可能触发并发轮转
  for (int t = 0; t < num_threads; ++t) {
    futures.push_back(
        std::async(std::launch::async, [t, messages_per_thread]() {
          for (int i = 0; i < messages_per_thread; ++i) {
            LOG_INFO("Rotation thread {} message {} - long content to trigger "
                     "rotation quickly",
                     t, i);
          }
        }));
  }

  // 等待完成
  for (auto &future : futures) {
    future.wait();
  }

  logger.Stop();
  LogTestUtils::WaitForLogProcessing(std::chrono::milliseconds(1000));

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);

  // 应该发生轮转但不应该崩溃
  EXPECT_GE(logFiles.size(), 1);
  EXPECT_LE(logFiles.size(), 5);

  // 验证所有文件都可以正常读取（没有损坏）
  for (const auto &file : logFiles) {
    std::string content = LogTestUtils::ReadFile(file);
    EXPECT_FALSE(content.empty()) << "Log file should not be empty: " << file;
  }
}

// ================================
// 压力测试
// ================================

class StressTest : public LogSystemTestBase {
protected:
  void SetUp() override {
    LogSystemTestBase::SetUp();
    LogTestUtils::SetupTestEnvironment(test_dir_, "FILE", "1");
  }
};

TEST_F(StressTest, HighVolumeLogging) {
  const int message_count = 50000;

  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  auto start_time = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < message_count; ++i) {
    LOG_INFO("High volume test message {} with timestamp {} and random data {}",
             i, std::chrono::steady_clock::now().time_since_epoch().count(),
             i * 3.14159);
  }

  logger.Stop();
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  LogTestUtils::WaitForLogProcessing(std::chrono::milliseconds(2000));

  // 性能验证
  int msgs_per_sec = (message_count * 1000) / (duration.count() + 1);
  EXPECT_GE(msgs_per_sec, 1000)
      << "Logging performance should be at least 1000 msgs/sec";

  // 完整性验证
  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  EXPECT_GE(logFiles.size(), 1);

  size_t total_lines = 0;
  for (const auto &file : logFiles) {
    total_lines += LogTestUtils::CountLines(file);
  }

  // 允许少量系统消息的误差
  EXPECT_GE(total_lines, message_count) << "Some messages may have been lost";

  std::cout << "High volume test: " << message_count << " messages in "
            << duration.count() << "ms (" << msgs_per_sec << " msgs/sec)"
            << std::endl;
}

TEST_F(StressTest, LongRunningStability) {
  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  logger.setMaxFileSize(10240); // 10KB for frequent rotation

  const auto test_duration = std::chrono::seconds(5); // 5秒测试
  const auto start_time = std::chrono::steady_clock::now();

  size_t message_count = 0;
  while (std::chrono::steady_clock::now() - start_time < test_duration) {
    LOG_INFO("Long running stability test message {} at {}", message_count++,
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count());

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  logger.Stop();
  LogTestUtils::WaitForLogProcessing();

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);

  // 应该生成多个文件（轮转正常工作）
  EXPECT_GE(logFiles.size(), 2)
      << "Long running test should trigger log rotation";

  // 验证消息数量合理
  EXPECT_GE(message_count, 500)
      << "Should process reasonable number of messages in 5 seconds";

  std::cout << "Long running test: " << message_count
            << " messages in 5 seconds, " << logFiles.size()
            << " log files created" << std::endl;
}

// ================================
// 错误处理测试
// ================================

class ErrorHandlingTest : public LogSystemTestBase {};

TEST_F(ErrorHandlingTest, InvalidLogDirectory) {
  // 尝试使用无效目录（只读或不存在的路径）
  std::string invalid_dir =
      "/root/invalid_log_dir_" + std::to_string(time(nullptr));
  LogTestUtils::SetupTestEnvironment(invalid_dir);

  // 应该优雅地处理错误，不崩溃
  EXPECT_NO_THROW({
    auto &logger = AsyncLogSystem::GetInstance();
    logger.Init();
    LOG_INFO("Test message to invalid directory");
    logger.Stop();
  });
}

TEST_F(ErrorHandlingTest, DiskFullSimulation) {
  // 这个测试比较难实现，简化为测试小文件限制
  config.rotation.maxFileSize = 100; // 100字节，很快就满
  config.rotation.maxFileCount = 1;  // 只保留1个文件

  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();
  logger.setMaxFileSize(100);
  logger.setMaxFileCount(1);

  // 写入大量数据，测试系统稳定性
  for (int i = 0; i < 50; ++i) {
    LOG_INFO("Disk full simulation test message {} with extra content", i);
  }

  EXPECT_NO_THROW(logger.Stop());

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  EXPECT_LE(logFiles.size(), 1)
      << "Should respect max file count even under stress";
}

// ================================
// 参数化测试
// ================================

class LogLevelParameterizedTest
    : public LogSystemTestBase,
      public WithParamInterface<std::tuple<int, std::vector<std::string>>> {};

TEST_P(LogLevelParameterizedTest, LogLevelFiltering) {
  int min_level = std::get<0>(GetParam());
  auto expected_levels = std::get<1>(GetParam());

  LogTestUtils::SetupTestEnvironment(test_dir_, "FILE",
                                     std::to_string(min_level));

  auto &logger = AsyncLogSystem::GetInstance();
  logger.Init();

  LOG_DEBUG("DEBUG message"); // Level 0
  LOG_INFO("INFO message");   // Level 1
  LOG_WARN("WARN message");   // Level 2
  LOG_ERROR("ERROR message"); // Level 3
  LOG_FATAL("FATAL message"); // Level 4

  logger.Stop();
  LogTestUtils::WaitForLogProcessing();

  auto logFiles = LogTestUtils::GetLogFiles(test_dir_);
  ASSERT_GE(logFiles.size(), 1);

  std::string content = LogTestUtils::ReadFile(logFiles[0]);

  for (const auto &level : expected_levels) {
    EXPECT_THAT(content, HasSubstr(level + " message"))
        << "Should contain " << level
        << " level messages when min_level=" << min_level;
  }
}

INSTANTIATE_TEST_SUITE_P(
    LogLevels, LogLevelParameterizedTest,
    Values(std::make_tuple(0, std::vector<std::string>{"DEBUG", "INFO", "WARN",
                                                       "ERROR", "FATAL"}),
           std::make_tuple(1, std::vector<std::string>{"INFO", "WARN", "ERROR",
                                                       "FATAL"}),
           std::make_tuple(2,
                           std::vector<std::string>{"WARN", "ERROR", "FATAL"}),
           std::make_tuple(3, std::vector<std::string>{"ERROR", "FATAL"}),
           std::make_tuple(4, std::vector<std::string>{"FATAL"})));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  // 设置全局测试环境
  std::cout << "Starting Google Test for Log System..." << std::endl;
  ConfigManagerImpl::GetInstance().printAll();

  return RUN_ALL_TESTS();
}
