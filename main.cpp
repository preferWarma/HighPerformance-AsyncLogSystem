#include "CoroutinePool.h"
#include <format>
#include <iostream>

using namespace lyf;
using namespace std::chrono_literals;

// 测试基本功能
Task<> test_basic() {
  std::cout << "Test 1: Basic coroutine\n";
  co_await co_sleep_for(100ms);
  std::cout << "After sleep\n";
}

// 测试Channel
Task<> test_channel() {
  std::cout << "\nTest 2: Channel\n";
  Channel<int> ch(5);

  // 生产者
  auto producer = [](Channel<int> &ch) -> Task<> {
    for (int i = 0; i < 3; ++i) {
      co_await ch.send(i);
      std::cout << std::format("Sent: {}\n", i);
    }
    ch.close();
  };

  // 消费者
  auto consumer = [](Channel<int> &ch) -> Task<> {
    while (auto val = co_await ch.recv()) {
      std::cout << std::format("Received: {}\n", *val);
    }
    std::cout << "Channel closed\n";
  };

  auto p = producer(ch);
  auto c = consumer(ch);

  co_await p;
  co_await c;
}

// 测试返回值
Task<int> compute(int x) {
  co_await co_sleep_for(50ms);
  co_return x * 2;
}

Task<> test_return_value() {
  std::cout << "\nTest 3: Return values\n";

  auto t1 = compute(5);
  auto t2 = compute(10);

  int r1 = co_await t1;
  int r2 = co_await t2;

  std::cout << std::format("Results: {} and {}\n", r1, r2);
}

// 测试WaitGroup
Task<> test_waitgroup() {
  std::cout << "\nTest 4: WaitGroup\n";

  WaitGroup wg;

  auto worker = [](int id, WaitGroup *wg) -> Task<> {
    std::cout << std::format("Worker {} started\n", id);
    co_await co_sleep_for(std::chrono::milliseconds(50 * id));
    std::cout << std::format("Worker {} done\n", id);
    wg->done();
  };

  for (int i = 1; i <= 3; ++i) {
    wg.add();
    go_co(worker, i, &wg);
  }

  co_await wg.wait();
  std::cout << "All workers completed\n";
}

// 测试互斥锁
Task<> test_mutex() {
  std::cout << "\nTest 5: Mutex\n";

  CoMutex mtx;
  int counter = 0;

  auto increment = [](CoMutex &mtx, int &counter, int id) -> Task<> {
    for (int i = 0; i < 2; ++i) {
      co_await mtx.lock();
      int old = counter;
      counter = old + 1;
      std::cout << std::format("Thread {}: {} -> {}\n", id, old, counter);
      mtx.unlock();
      co_await co_sleep_for(10ms);
    }
  };

  auto t1 = increment(mtx, counter, 1);
  auto t2 = increment(mtx, counter, 2);

  co_await t1;
  co_await t2;

  std::cout << std::format("Final counter: {}\n", counter);
}

// 测试异常处理
Task<int> may_throw(bool should_throw) {
  if (should_throw) {
    throw std::runtime_error("Test exception");
  }
  co_return 99;
}

Task<> test_exception() {
  std::cout << "\nTest 6: Exception handling\n";

  // 正常情况
  try {
    int val = co_await may_throw(false);
    std::cout << std::format("Got value: {}\n", val);
  } catch (const std::exception &e) {
    std::cout << std::format("Caught: {}\n", e.what());
  }

  // 异常情况
  try {
    int val = co_await may_throw(true);
    std::cout << std::format("Should not see this: {}\n", val);
  } catch (const std::exception &e) {
    std::cout << std::format("Caught expected exception: {}\n", e.what());
  }
}

// 主测试函数
Task<> run_all_tests() {
  std::cout << "=== Running Coroutine Pool Tests ===\n";

  co_await test_basic();
  co_await test_channel();
  co_await test_return_value();
  co_await test_waitgroup();
  co_await test_mutex();
  co_await test_exception();

  std::cout << "\n=== All Tests Completed ===\n";
}

int main() {
  try {
    auto task = run_all_tests();
    task.get(); // 阻塞等待所有测试完成
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}