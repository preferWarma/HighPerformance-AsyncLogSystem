// logger_comparison_benchmark.cpp
#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <mutex>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/types.h>
#include <unistd.h>
#elif __linux__
#include <fstream>
#endif

#include "LogSystem.h"  // 你的系统

// ============================================================
// 测试配置
// ============================================================

// RAII包装器确保spdlog正确清理
class SpdlogGuard {
public:
    SpdlogGuard() {
        spdlog::shutdown();
        spdlog::drop_all();
    }
    
    ~SpdlogGuard() {
        spdlog::shutdown();
        spdlog::drop_all();
    }
};

void setup_your_logger() {
    auto& cfg = lyf::Config::GetInstance();
    cfg.output.toConsole = false;
    cfg.output.toFile = true;
    cfg.output.logRootDir = "./bench_your_log";
    cfg.output.minLogLevel = 0;
    cfg.cloud.enable = false;
    cfg.performance.fileFlushInterval = std::chrono::milliseconds(1000);
    cfg.performance.fileBatchSize = 1024;
    cfg.basic.maxFileLogQueueSize = 8192;
    
    lyf::AsyncLogSystem::GetInstance().Init();
}

void setup_spdlog() {
    static std::mutex spdlog_mutex;
    std::lock_guard<std::mutex> lock(spdlog_mutex);
    
    // 配置相似的异步logger
    spdlog::drop_all();
    spdlog::shutdown();
    
    try {
        spdlog::init_thread_pool(8192, 1);  // 队列大小，线程数
        auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
            "async_logger", "./bench_spdlog/log.txt");
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_default_logger(logger);
    } catch (const spdlog::spdlog_ex& e) {
        // 如果仍然失败，使用备用方案
        spdlog::drop_all();
        spdlog::shutdown();
        
        // 使用不同的logger名称
        spdlog::init_thread_pool(8192, 1);
        auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
            "async_logger_v2", "./bench_spdlog/log.txt");
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_default_logger(logger);
    }
}

// ============================================================
// 1. 单线程吞吐量测试
// ============================================================

static void BM_YourLogger_SingleThread(benchmark::State& state) {
    setup_your_logger();
    
    for (auto _ : state) {
        for (int i = 0; i < 1000; ++i) {
            LOG_INFO("Message number {} with some text", i);
        }
    }
    
    lyf::AsyncLogSystem::GetInstance().Stop();
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_YourLogger_SingleThread);

static void BM_Spdlog_SingleThread(benchmark::State& state) {
    setup_spdlog();
    
    for (auto _ : state) {
        for (int i = 0; i < 1000; ++i) {
            spdlog::info("Message number {} with some text", i);
        }
    }
    
    spdlog::shutdown();
    spdlog::drop_all();
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_Spdlog_SingleThread);

// ============================================================
// 2. 多线程并发测试
// ============================================================

static void BM_YourLogger_MultiThread(benchmark::State& state) {
    if (state.thread_index() == 0) {
        setup_your_logger();
    }
    
    for (auto _ : state) {
        for (int i = 0; i < 100; ++i) {
            LOG_INFO("Thread {} message {}", state.thread_index(), i);
        }
    }
    
    if (state.thread_index() == 0) {
        lyf::AsyncLogSystem::GetInstance().Stop();
    }
    
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_YourLogger_MultiThread)->Threads(8);

static void BM_Spdlog_MultiThread(benchmark::State& state) {
    if (state.thread_index() == 0) {
        setup_spdlog();
    }
    
    for (auto _ : state) {
        for (int i = 0; i < 100; ++i) {
            spdlog::info("Thread {} message {}", state.thread_index(), i);
        }
    }
    
    if (state.thread_index() == 0) {
        spdlog::shutdown();
        spdlog::drop_all();
    }
    
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Spdlog_MultiThread)->Threads(8);

// ============================================================
// 3. 延迟测试（单条消息）
// ============================================================

static void BM_YourLogger_Latency(benchmark::State& state) {
    setup_your_logger();
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        LOG_INFO("Latency test message");
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }
    
    lyf::AsyncLogSystem::GetInstance().Stop();
}
BENCHMARK(BM_YourLogger_Latency)->UseManualTime();

static void BM_Spdlog_Latency(benchmark::State& state) {
    setup_spdlog();
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        spdlog::info("Latency test message");
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1e9);
    }
    
    spdlog::shutdown();
    spdlog::drop_all();
}
BENCHMARK(BM_Spdlog_Latency)->UseManualTime();

// ============================================================
// 4. 格式化性能测试
// ============================================================

static void BM_YourLogger_Formatting(benchmark::State& state) {
    // 不写入文件，只测试格式化
    auto& cfg = lyf::Config::GetInstance();
    cfg.output.toFile = false;
    cfg.output.toConsole = false;
    lyf::AsyncLogSystem::GetInstance().Init();
    
    for (auto _ : state) {
        for (int i = 0; i < 100; ++i) {
            LOG_INFO("Int: {}, Float: {}, String: {}, Bool: {}", 
                    i, 3.14159 * i, "test string", i % 2 == 0);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_YourLogger_Formatting);

static void BM_Spdlog_Formatting(benchmark::State& state) {
    // 使用 null sink，只测试格式化
    spdlog::drop("null");  // 如果存在则删除旧的logger
    auto null_logger = spdlog::create<spdlog::sinks::null_sink_mt>("null");
    spdlog::set_default_logger(null_logger);
    
    for (auto _ : state) {
        for (int i = 0; i < 100; ++i) {
            spdlog::info("Int: {}, Float: {}, String: {}, Bool: {}", 
                        i, 3.14159 * i, "test string", i % 2 == 0);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Spdlog_Formatting);

// ============================================================
// 5. 内存使用测试
// ============================================================

void PrintMemoryUsage(const std::string& label) {
#ifdef __APPLE__
    task_t task = mach_task_self();
    struct task_basic_info info;
    mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
    kern_return_t kerr = task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &size);
    
    if (kerr == KERN_SUCCESS) {
        double memory_mb = static_cast<double>(info.resident_size) / 1024.0 / 1024.0;
        std::cout << label << " - Memory: " << memory_mb << " MB (resident)" << std::endl;
    } else {
        std::cout << label << " - Memory: Unable to get memory info" << std::endl;
    }
#elif __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") == 0) {
            std::cout << label << " - " << line << std::endl;
            break;
        }
    }
#else
    std::cout << label << " - Memory: Unsupported platform" << std::endl;
#endif
}

static void BM_YourLogger_Memory(benchmark::State& state) {
    setup_your_logger();
    
    PrintMemoryUsage("YourLogger Before");
    
    for (auto _ : state) {
        for (int i = 0; i < 10000; ++i) {
            LOG_INFO("Memory test message {}", i);
        }
    }
    
    PrintMemoryUsage("YourLogger After");
    
    lyf::AsyncLogSystem::GetInstance().Stop();
}
BENCHMARK(BM_YourLogger_Memory)->Iterations(100);

static void BM_Spdlog_Memory(benchmark::State& state) {
    setup_spdlog();
    
    PrintMemoryUsage("Spdlog Before");
    
    for (auto _ : state) {
        for (int i = 0; i < 10000; ++i) {
            spdlog::info("Memory test message {}", i);
        }
    }
    
    PrintMemoryUsage("Spdlog After");
    
    spdlog::shutdown();
    spdlog::drop_all();
}
BENCHMARK(BM_Spdlog_Memory)->Iterations(100);

// ============================================================
// 6. 长时间稳定性测试 (Long-running Stability Test)
// ============================================================

static void BM_YourLogger_Stability(benchmark::State& state) {
    setup_your_logger();
    
    const size_t totalMessages = 1000000; // 100万条消息
    const size_t batchSize = 10000;
    
    for (auto _ : state) {
        state.PauseTiming();
        auto startTime = std::chrono::steady_clock::now();
        state.ResumeTiming();
        
        for (size_t batch = 0; batch < totalMessages / batchSize; ++batch) {
            for (size_t i = 0; i < batchSize; ++i) {
                LOG_INFO("Stability test message batch {} index {}", 
                        batch, i);
            }
            
            // 每批次后短暂休眠，模拟真实负载
            if (batch % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        
        state.PauseTiming();
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime);
        
        std::cout << "YourLogger: Processed " << totalMessages 
                  << " messages in " << duration.count() << "ms\n";
        state.ResumeTiming();
    }
    
    lyf::AsyncLogSystem::GetInstance().Stop();
}
BENCHMARK(BM_YourLogger_Stability)->Unit(benchmark::kSecond)->Iterations(3);

static void BM_Spdlog_Stability(benchmark::State& state) {
    setup_spdlog();
    
    const size_t totalMessages = 1000000; // 100万条消息
    const size_t batchSize = 10000;
    
    for (auto _ : state) {
        state.PauseTiming();
        auto startTime = std::chrono::steady_clock::now();
        state.ResumeTiming();
        
        for (size_t batch = 0; batch < totalMessages / batchSize; ++batch) {
            for (size_t i = 0; i < batchSize; ++i) {
                spdlog::info("Stability test message batch {} index {}", 
                           batch, i);
            }
            
            // 每批次后短暂休眠，模拟真实负载
            if (batch % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        
        state.PauseTiming();
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime);
        
        std::cout << "Spdlog: Processed " << totalMessages 
                  << " messages in " << duration.count() << "ms\n";
        state.ResumeTiming();
    }
    
    spdlog::shutdown();
    spdlog::drop_all();
}
BENCHMARK(BM_Spdlog_Stability)->Unit(benchmark::kSecond)->Iterations(3);

// ============================================================
// 7. 大消息体量测试 (Large Message Payload Test)
// ============================================================

static void BM_YourLogger_LargePayload(benchmark::State& state) {
    setup_your_logger();
    
    const size_t messageSize = state.range(0);
    const size_t messageCount = state.range(1);
    std::string largeMessage(messageSize, 'X');
    
    for (auto _ : state) {
        for (size_t i = 0; i < messageCount; ++i) {
            LOG_INFO("Large payload {}: {}", i, largeMessage);
        }
    }
    
    state.SetBytesProcessed(state.iterations() * messageCount * messageSize);
    lyf::AsyncLogSystem::GetInstance().Stop();
}
BENCHMARK(BM_YourLogger_LargePayload)
    ->Args({1024, 1000})      // 1KB消息，1000条
    ->Args({8192, 1000})      // 8KB消息，1000条
    ->Args({16384, 500})      // 16KB消息，500条
    ->Args({65536, 100})      // 64KB消息，100条
    ->Unit(benchmark::kMillisecond);

static void BM_Spdlog_LargePayload(benchmark::State& state) {
    setup_spdlog();
    
    const size_t messageSize = state.range(0);
    const size_t messageCount = state.range(1);
    std::string largeMessage(messageSize, 'X');
    
    for (auto _ : state) {
        for (size_t i = 0; i < messageCount; ++i) {
            spdlog::info("Large payload {}: {}", i, largeMessage);
        }
    }
    
    state.SetBytesProcessed(state.iterations() * messageCount * messageSize);
    spdlog::shutdown();
    spdlog::drop_all();
}
BENCHMARK(BM_Spdlog_LargePayload)
    ->Args({1024, 1000})
    ->Args({8192, 1000})
    ->Args({16384, 500})
    ->Args({65536, 100})
    ->Unit(benchmark::kMillisecond);

// ============================================================
// 8. 极端场景测试 (Extreme Scenario Test)
// ============================================================

static void BM_YourLogger_Extreme(benchmark::State& state) {
    setup_your_logger();
    
    const int threadCount = state.range(0);
    const size_t messagesPerThread = state.range(1);
    const size_t payloadSize = state.range(2);
    
    std::string payload(payloadSize, 'E');
    
    if (state.thread_index() == 0) {
        std::cout << "Extreme test: " << threadCount << " threads, "
                  << messagesPerThread << " messages/thread, "
                  << payloadSize << " bytes/message\n";
    }
    
    for (auto _ : state) {
        for (size_t i = 0; i < messagesPerThread; ++i) {
            LOG_INFO("Extreme test thread {} message {}: {}", 
                    state.thread_index(), i, payload);
        }
    }
    
    state.SetBytesProcessed(state.iterations() * messagesPerThread * payloadSize);
}
BENCHMARK(BM_YourLogger_Extreme)
    ->Args({8, 10000, 1024})    // 8线程，每线程1万条，1KB
    ->Args({16, 5000, 2048})    // 16线程，每线程5000条，2KB
    ->Args({32, 1000, 4096})    // 32线程，每线程1000条，4KB
    ->Threads(8)->Threads(16)->Threads(32)
    ->Unit(benchmark::kSecond);

static void BM_Spdlog_Extreme(benchmark::State& state) {
    setup_spdlog();
    
    const int threadCount = state.range(0);
    const size_t messagesPerThread = state.range(1);
    const size_t payloadSize = state.range(2);
    
    std::string payload(payloadSize, 'E');
    
    if (state.thread_index() == 0) {
        std::cout << "Extreme test: " << threadCount << " threads, "
                  << messagesPerThread << " messages/thread, "
                  << payloadSize << " bytes/message\n";
    }
    
    for (auto _ : state) {
        for (size_t i = 0; i < messagesPerThread; ++i) {
            spdlog::info("Extreme test thread {} message {}: {}", 
                        state.thread_index(), i, payload);
        }
    }
    
    state.SetBytesProcessed(state.iterations() * messagesPerThread * payloadSize);
    spdlog::shutdown();
    spdlog::drop_all();
}
BENCHMARK(BM_Spdlog_Extreme)
    ->Args({8, 10000, 1024})
    ->Args({16, 5000, 2048})
    ->Args({32, 1000, 4096})
    ->Threads(8)->Threads(16)->Threads(32)
    ->Unit(benchmark::kSecond);

BENCHMARK_MAIN();