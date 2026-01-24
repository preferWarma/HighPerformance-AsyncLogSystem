#include "LogSystem.h"
#include "tool/Timer.h"

using namespace lyf;
int main() {
  stopwatch timer(stopwatch::TimeType::ms);
  timer.start();
  LOG_DEBUG("this is a debug log, 10 + 20 = {}", 10 + 20);
  LOG_INFO("this is a info log, 10 + 20 = {}", 10 + 20);
  LOG_WARN("this is a warn log, 10 + 20 = {}", 10 + 20);
  LOG_ERROR("this is a error log, 10 + 20 = {}", 10 + 20);
  timer.stop();
  LOG_INFO("total time: {} ms", timer.duration());
}