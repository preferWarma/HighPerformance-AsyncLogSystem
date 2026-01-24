#include "LogSystem.h"
#include <iostream>
#include <thread>

using namespace lyf;

int main() {
  INIT_LOG_SYSTEM("config.toml");

  std::thread t1([]() {
    LOG_INFO("Hello, LogSystem!");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  });

  std::thread t2([]() {
    LOG_WARN("LogSystem Warn! {}", 123);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  });

  std::thread t3([]() {
    LOG_ERROR("LogSystem Error! {}", 123);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  });

  t1.join();
  t2.join();
  t3.join();

  AsyncLogSystem::Instance().Flush();
  std::cout << "LogSystem Flushed" << std::endl;

  AsyncLogSystem::Instance().Stop();
  std::cout << "LogSystem Stopped" << std::endl;
}