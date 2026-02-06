#include "LogConfig.h"
#include "LogMessage.h"
#include "LogFormatter.h"
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(LogFormatterTest, FormatsCoreFields) {
  lyf::LogConfig cfg;
  cfg.SetTimeFormat("%Y");
  lyf::BufferPool pool(1);
  auto *buf = pool.Alloc();
  std::string payload = "hello";
  std::copy(payload.begin(), payload.end(), buf->data);
  buf->length = payload.size();
  buf->data[buf->length] = '\0';
  auto now = std::chrono::system_clock::now().time_since_epoch().count();
  lyf::LogMessage msg(lyf::LogLevel::INFO, "file.cpp", 123, static_cast<size_t>(7),
                      now, buf, &pool);

  lyf::LogFormatter formatter;
  formatter.SetConfig(&cfg);
  std::vector<char> output_buf;
  formatter.Format(msg, output_buf);
  std::string output(output_buf.begin(), output_buf.end());

  auto first_space = output.find(' ');
  EXPECT_NE(first_space, std::string::npos);
  EXPECT_NE(output.find("INFO"), std::string::npos);
  EXPECT_NE(output.find("file.cpp:123"), std::string::npos);
  EXPECT_NE(output.find("hello"), std::string::npos);
  EXPECT_EQ(output.back(), '\n');
}
