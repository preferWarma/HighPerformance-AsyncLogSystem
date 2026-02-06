#include "LogConfig.h"
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

TEST(LogConfigTest, ParseLevelFallback) {
  EXPECT_EQ(lyf::ParseLevel("BAD"), lyf::LogLevel::INFO);
  EXPECT_EQ(lyf::ParseLevel("DEBUG"), lyf::LogLevel::DEBUG);
  EXPECT_EQ(lyf::LevelToString(lyf::LogLevel::WARN), "WARN");
}

TEST(LogConfigTest, LoadFromMissingFileReturnsFalse) {
  lyf::LogConfig cfg;
  auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("missing_config_" + std::to_string(ts) + ".toml");
  bool ok = cfg.LoadFromFile(path.string());
  EXPECT_FALSE(ok);
}

TEST(LogConfigTest, RejectsEmptyTimeFormat) {
  lyf::LogConfig cfg;
  auto before = cfg.GetTimeFormat();
  cfg.SetTimeFormat("");
  EXPECT_EQ(cfg.GetTimeFormat(), before);
}
