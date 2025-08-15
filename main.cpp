#include "Config.h"
#include "LogSystem.h"
using namespace lyf;
int main() {
  auto &cfg = Config::GetInstance();
  auto &helper = JsonHelper::GetInstance();
  helper.PrintAllConfig();
  string t;
  while (std::cin >> t) {
    LOG_INFO("input {}", t);
    helper.PrintAllConfig();
  }
}