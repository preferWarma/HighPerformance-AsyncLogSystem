// // HttpSink使用示例
// #include "LogSystem.h"
// #include <iostream>

// using namespace lyf;

// int main() {
//   // 初始化日志系统
//   INIT_LOG_SYSTEM("config.toml");

//   std::cout << "=== HttpSink使用示例 ===" << std::endl;
//   std::cout << "当前配置的Sink数量: "
//             << AsyncLogSystem::Instance().GetSinkManager().GetSinkCount()
//             << std::endl;

//   // 方法1: 通过配置文件启用HttpSink
//   // 在config.toml中设置 [http] 部分的参数和 toHttp = true

//   // 方法2: 动态添加HttpSink
//   auto httpSink = std::make_unique<HttpSink>();
//   HttpSink::HttpConfig httpConfig;
//   httpConfig.url = "http://localhost:8080"; // 设置HTTP服务器地址
//   httpConfig.endpoint = "/api/logs";        // API端点
//   httpConfig.contentType = "application/json";
//   httpConfig.timeout_sec = 10;
//   httpConfig.maxRetries = 3;
//   httpConfig.batchSize = 50;
//   httpConfig.bufferSize_kb = 32;
//   httpConfig.enableCompression = false;
//   httpConfig.enableAsync = true;
//   httpSink->SetHttpConfig(httpConfig);

//   // 添加到Sink管理器
//   if (AsyncLogSystem::Instance().AddSink(std::move(httpSink))) {
//     std::cout << "HttpSink已成功添加到日志系统" << std::endl;
//   } else {
//     std::cout << "HttpSink添加失败" << std::endl;
//   }

//   std::cout << "添加HttpSink后的Sink数量: "
//             << AsyncLogSystem::Instance().GetSinkManager().GetSinkCount()
//             << std::endl;

//   // 记录一些测试日志
//   LOG_DEBUG("这是一条DEBUG级别的日志");
//   LOG_INFO("这是一条INFO级别的日志");
//   LOG_WARN("这是一条WARNING级别的日志");
//   LOG_ERROR("这是一条ERROR级别的日志");

//   // 批量日志测试
//   for (int i = 0; i < 100; ++i) {
//     LOG_INFO("批量日志测试 - 消息编号: {}", i);
//   }

//   // 刷新所有日志
//   AsyncLogSystem::Instance().Flush();

//   std::cout << "日志已刷新" << std::endl;

//   return 0;
// }
