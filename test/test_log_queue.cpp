#include "LogMessage.h"
#include "LogQue.h"
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <vector>

static lyf::LogMessage MakeMessage(lyf::BufferPool &pool,
                                   std::string_view text) {
  auto *buf = pool.Alloc();
  std::copy(text.begin(), text.end(), buf->data);
  buf->length = text.size();
  buf->data[buf->length] = '\0';
  auto now = std::chrono::system_clock::now().time_since_epoch().count();
  return lyf::LogMessage(lyf::LogLevel::INFO, "queue.cpp", 1,
                         static_cast<size_t>(1), now, buf, &pool);
}

TEST(LogQueueTest, DropPolicyRejectsWhenFull) {
  lyf::QueConfig cfg(1, lyf::QueueFullPolicy::DROP, 0);
  lyf::LogQueue queue(cfg);
  lyf::BufferPool pool(2);
  auto msg1 = MakeMessage(pool, "a");
  auto msg2 = MakeMessage(pool, "b");

  EXPECT_TRUE(queue.Push(std::move(msg1)));
  EXPECT_FALSE(queue.Push(std::move(msg2)));

  std::vector<lyf::LogMessage> out;
  queue.PopBatch(out, 10);
  out.clear();
}

TEST(LogQueueTest, ZeroCapacityDoesNotBackpressure) {
  lyf::QueConfig cfg(0, lyf::QueueFullPolicy::DROP, 0);
  lyf::LogQueue queue(cfg);
  lyf::BufferPool pool(4);
  auto msg1 = MakeMessage(pool, "a");
  auto msg2 = MakeMessage(pool, "b");

  EXPECT_TRUE(queue.Push(std::move(msg1)));
  EXPECT_TRUE(queue.Push(std::move(msg2)));

  std::vector<lyf::LogMessage> out;
  queue.PopBatch(out, 10);
}
