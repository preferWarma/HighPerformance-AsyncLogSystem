#include "FastFormater.h"
#include <benchmark/benchmark.h>
#include <format>
#include <string_view>

// 检查是否支持 fmt 库
#ifdef __has_include
#if __has_include(<fmt/format.h>)
#include <fmt/format.h>
#define HAS_FMT_LIBRARY 1
#endif
#endif

// 测试用例数据
constexpr char kSimpleFormat[] = "Hello, World!";
constexpr char kOneArgFormat[] = "Hello, {}!";
constexpr char kTwoArgsFormat[] = "Hello, {}! You have {} messages.";
constexpr char kComplexFormat[] =
    "User {} logged in at {} from IP {} with session ID {}";
constexpr char kNumericFormat[] = "Result: {}, Count: {}, Average: {}";

// 测试无参数格式化
static void BM_NoArgs_Custom(benchmark::State &state) {
  for (auto _ : state) {
    auto result = lyf::FormatMessage(kSimpleFormat);
    benchmark::DoNotOptimize(result);
  }
}

// 编译期格式化测试
static void BM_NoArgs_Static(benchmark::State &state) {
  for (auto _ : state) {
    auto result = lyf::FormatMessage<kSimpleFormat>();
    benchmark::DoNotOptimize(result);
  }
}

static void BM_NoArgs_StdFormat(benchmark::State &state) {
  for (auto _ : state) {
    auto result = std::format(kSimpleFormat);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_NoArgs_StdVFormat(benchmark::State &state) {
  for (auto _ : state) {
    auto result = std::vformat(kSimpleFormat, std::make_format_args());
    benchmark::DoNotOptimize(result);
  }
}

#ifdef HAS_FMT_LIBRARY
static void BM_NoArgs_FmtFormat(benchmark::State &state) {
  for (auto _ : state) {
    auto result = fmt::format(kSimpleFormat);
    benchmark::DoNotOptimize(result);
  }
}
#endif

// 测试单参数格式化
static void BM_OneArg_Custom(benchmark::State &state) {
  const char *name = "Alice";
  for (auto _ : state) {
    auto result = lyf::FormatMessage(kOneArgFormat, name);
    benchmark::DoNotOptimize(result);
  }
}

// 编译期格式化测试
static void BM_OneArg_Static(benchmark::State &state) {
  const char *name = "Alice";
  for (auto _ : state) {
    auto result = lyf::FormatMessage<kOneArgFormat>(name);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_OneArg_StdFormat(benchmark::State &state) {
  const char *name = "Alice";
  for (auto _ : state) {
    auto result = std::format(kOneArgFormat, name);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_OneArg_StdVFormat(benchmark::State &state) {
  const char *name = "Alice";
  for (auto _ : state) {
    auto result = std::vformat(kOneArgFormat, std::make_format_args(name));
    benchmark::DoNotOptimize(result);
  }
}

#ifdef HAS_FMT_LIBRARY
static void BM_OneArg_FmtFormat(benchmark::State &state) {
  const char *name = "Alice";
  for (auto _ : state) {
    auto result = fmt::format(kOneArgFormat, name);
    benchmark::DoNotOptimize(result);
  }
}
#endif

// 测试双参数格式化
static void BM_TwoArgs_Custom(benchmark::State &state) {
  const char *name = "Bob";
  int count = 42;
  for (auto _ : state) {
    auto result = lyf::FormatMessage(kTwoArgsFormat, name, count);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_TwoArgs_Static(benchmark::State &state) {
  const char *name = "Bob";
  int count = 42;
  for (auto _ : state) {
    auto result = lyf::FormatMessage<kTwoArgsFormat>(name, count);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_TwoArgs_StdFormat(benchmark::State &state) {
  const char *name = "Bob";
  int count = 42;
  for (auto _ : state) {
    auto result = std::format(kTwoArgsFormat, name, count);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_TwoArgs_StdVFormat(benchmark::State &state) {
  const char *name = "Bob";
  int count = 42;
  for (auto _ : state) {
    auto result =
        std::vformat(kTwoArgsFormat, std::make_format_args(name, count));
    benchmark::DoNotOptimize(result);
  }
}

#ifdef HAS_FMT_LIBRARY
static void BM_TwoArgs_FmtFormat(benchmark::State &state) {
  const char *name = "Bob";
  int count = 42;
  for (auto _ : state) {
    auto result = fmt::format(kTwoArgsFormat, name, count);
    benchmark::DoNotOptimize(result);
  }
}
#endif

// 测试复杂格式化（4个参数）
static void BM_Complex_Custom(benchmark::State &state) {
  const char *user = "charlie";
  const char *time = "2024-01-15 14:30:00";
  const char *ip = "192.168.1.100";
  const char *session = "sess_123456789";
  for (auto _ : state) {
    auto result = lyf::FormatMessage(kComplexFormat, user, time, ip, session);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_Complex_Static(benchmark::State &state) {
  const char *user = "charlie";
  const char *time = "2024-01-15 14:30:00";
  const char *ip = "192.168.1.100";
  const char *session = "sess_123456789";
  for (auto _ : state) {
    auto result = lyf::FormatMessage<kComplexFormat>(user, time, ip, session);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_Complex_StdFormat(benchmark::State &state) {
  const char *user = "charlie";
  const char *time = "2024-01-15 14:30:00";
  const char *ip = "192.168.1.100";
  const char *session = "sess_123456789";
  for (auto _ : state) {
    auto result = std::format(kComplexFormat, user, time, ip, session);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_Complex_StdVFormat(benchmark::State &state) {
  const char *user = "charlie";
  const char *time = "2024-01-15 14:30:00";
  const char *ip = "192.168.1.100";
  const char *session = "sess_123456789";
  for (auto _ : state) {
    auto result = std::vformat(kComplexFormat,
                               std::make_format_args(user, time, ip, session));
    benchmark::DoNotOptimize(result);
  }
}

#ifdef HAS_FMT_LIBRARY
static void BM_Complex_FmtFormat(benchmark::State &state) {
  const char *user = "charlie";
  const char *time = "2024-01-15 14:30:00";
  const char *ip = "192.168.1.100";
  const char *session = "sess_123456789";
  for (auto _ : state) {
    auto result = fmt::format(kComplexFormat, user, time, ip, session);
    benchmark::DoNotOptimize(result);
  }
}
#endif

// 测试数值格式化
static void BM_Numeric_Custom(benchmark::State &state) {
  double result_value = 3.14159265359;
  int count = 1000;
  double average = 0.123456789;
  for (auto _ : state) {
    auto result =
        lyf::FormatMessage(kNumericFormat, result_value, count, average);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_Numeric_Static(benchmark::State &state) {
  double result_value = 3.14159265359;
  int count = 1000;
  double average = 0.123456789;
  for (auto _ : state) {
    auto result =
        lyf::FormatMessage<kNumericFormat>(result_value, count, average);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_Numeric_StdFormat(benchmark::State &state) {
  double result_value = 3.14159265359;
  int count = 1000;
  double average = 0.123456789;
  for (auto _ : state) {
    auto result = std::format(kNumericFormat, result_value, count, average);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_Numeric_StdVFormat(benchmark::State &state) {
  double result_value = 3.14159265359;
  int count = 1000;
  double average = 0.123456789;
  for (auto _ : state) {
    auto result = std::vformat(
        kNumericFormat, std::make_format_args(result_value, count, average));
    benchmark::DoNotOptimize(result);
  }
}

#ifdef HAS_FMT_LIBRARY
static void BM_Numeric_FmtFormat(benchmark::State &state) {
  double result_value = 3.14159265359;
  int count = 1000;
  double average = 0.123456789;
  for (auto _ : state) {
    auto result = fmt::format(kNumericFormat, result_value, count, average);
    benchmark::DoNotOptimize(result);
  }
}
#endif

// 测试长字符串格式化
static void BM_LongString_Custom(benchmark::State &state) {
  const char *long_text =
      "This is a very long text that contains multiple words and characters. "
      "It is designed to test the performance of string formatting with longer "
      "input strings. The goal is to see how different formatting libraries "
      "handle "
      "larger text data. We expect that the performance difference will be "
      "more "
      "noticeable with longer strings compared to short ones.";
  for (auto _ : state) {
    auto result = lyf::FormatMessage("Log message: {}", long_text);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_LongString_Static(benchmark::State &state) {
  const char *long_text =
      "This is a very long text that contains multiple words and characters. "
      "It is designed to test the performance of string formatting with longer "
      "input strings. The goal is to see how different formatting libraries "
      "handle "
      "larger text data. We expect that the performance difference will be "
      "more "
      "noticeable with longer strings compared to short ones.";
  for (auto _ : state) {
    auto result = lyf::FormatMessage<"Log message: {}">(long_text);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_LongString_StdFormat(benchmark::State &state) {
  const char *long_text =
      "This is a very long text that contains multiple words and characters. "
      "It is designed to test the performance of string formatting with longer "
      "input strings. The goal is to see how different formatting libraries "
      "handle "
      "larger text data. We expect that the performance difference will be "
      "more "
      "noticeable with longer strings compared to short ones.";
  for (auto _ : state) {
    auto result = std::format("Log message: {}", long_text);
    benchmark::DoNotOptimize(result);
  }
}

#ifdef HAS_FMT_LIBRARY
static void BM_LongString_FmtFormat(benchmark::State &state) {
  const char *long_text =
      "This is a very long text that contains multiple words and characters. "
      "It is designed to test the performance of string formatting with longer "
      "input strings. The goal is to see how different formatting libraries "
      "handle "
      "larger text data. We expect that the performance difference will be "
      "more "
      "noticeable with longer strings compared to short ones.";
  for (auto _ : state) {
    auto result = fmt::format("Log message: {}", long_text);
    benchmark::DoNotOptimize(result);
  }
}
#endif

// 注册所有基准测试
BENCHMARK(BM_NoArgs_Custom);
BENCHMARK(BM_NoArgs_Static);
BENCHMARK(BM_NoArgs_StdFormat);
BENCHMARK(BM_NoArgs_StdVFormat);
#ifdef HAS_FMT_LIBRARY
BENCHMARK(BM_NoArgs_FmtFormat);
#endif

BENCHMARK(BM_OneArg_Custom);
BENCHMARK(BM_OneArg_Static);
BENCHMARK(BM_OneArg_StdFormat);
BENCHMARK(BM_OneArg_StdVFormat);
#ifdef HAS_FMT_LIBRARY
BENCHMARK(BM_OneArg_FmtFormat);
#endif

BENCHMARK(BM_TwoArgs_Custom);
BENCHMARK(BM_TwoArgs_Static);
BENCHMARK(BM_TwoArgs_StdFormat);
BENCHMARK(BM_TwoArgs_StdVFormat);
#ifdef HAS_FMT_LIBRARY
BENCHMARK(BM_TwoArgs_FmtFormat);
#endif

BENCHMARK(BM_Complex_Custom);
BENCHMARK(BM_Complex_Static);
BENCHMARK(BM_Complex_StdFormat);
BENCHMARK(BM_Complex_StdVFormat);
#ifdef HAS_FMT_LIBRARY
BENCHMARK(BM_Complex_FmtFormat);
#endif

BENCHMARK(BM_Numeric_Custom);
BENCHMARK(BM_Numeric_Static);
BENCHMARK(BM_Numeric_StdFormat);
BENCHMARK(BM_Numeric_StdVFormat);
#ifdef HAS_FMT_LIBRARY
BENCHMARK(BM_Numeric_FmtFormat);
#endif

BENCHMARK(BM_LongString_Custom);
BENCHMARK(BM_LongString_Static);
BENCHMARK(BM_LongString_StdFormat);
#ifdef HAS_FMT_LIBRARY
BENCHMARK(BM_LongString_FmtFormat);
#endif

BENCHMARK_MAIN();