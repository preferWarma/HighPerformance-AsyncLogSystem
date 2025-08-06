#pragma once

#include <string>
#include <string_view>
#include <charconv>
#include <cstring>
#include <type_traits>

namespace lyf {

// ============================================================
// 高性能 Header-Only 格式化实现
// 特点：
// 1. 零依赖，纯 header-only
// 2. 最小化内存分配
// 3. 优化的类型转换
// 4. 内联友好
// ============================================================

namespace detail {

// ---------- 整数转字符串（最快路径） ----------
template<typename T>
inline size_t int_to_chars(char* buffer, size_t size, T value) {
    if constexpr (std::is_integral_v<T>) {
        // 使用 std::to_chars (C++17)
        auto [ptr, ec] = std::to_chars(buffer, buffer + size, value);
        return (ec == std::errc{}) ? (ptr - buffer) : 0;
    }
    return 0;
}

// ---------- 浮点数转字符串 ----------
inline size_t float_to_chars(char* buffer, size_t size, double value) {
#if __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
    // 完整的 to_chars 支持（某些编译器可能不支持浮点数）
    auto [ptr, ec] = std::to_chars(buffer, buffer + size, value);
    return (ec == std::errc{}) ? (ptr - buffer) : 0;
#else
    // 回退到 snprintf
    int ret = std::snprintf(buffer, size, "%.6g", value);
    return (ret > 0 && ret < static_cast<int>(size)) ? ret : 0;
#endif
}

// ---------- 指针转字符串 ----------
inline size_t ptr_to_chars(char* buffer, size_t size, const void* ptr) {
    // 格式化为 0x 开头的十六进制
    if (size < 3) return 0;
    
    buffer[0] = '0';
    buffer[1] = 'x';
    
    uintptr_t value = reinterpret_cast<uintptr_t>(ptr);
    auto [end, ec] = std::to_chars(buffer + 2, buffer + size, value, 16);
    return (ec == std::errc{}) ? (end - buffer) : 0;
}

// ---------- 布尔转字符串 ----------
inline size_t bool_to_chars(char* buffer, size_t size, bool value) {
    constexpr const char* true_str = "true";
    constexpr const char* false_str = "false";
    const char* str = value ? true_str : false_str;
    size_t len = value ? 4 : 5;
    
    if (len <= size) {
        std::memcpy(buffer, str, len);
        return len;
    }
    return 0;
}

// ---------- 通用 Appender：根据类型选择最优路径 ----------
template<typename T>
struct Appender {
    static void append(std::string& out, T&& value) {
        using DecayT = std::decay_t<T>;
        
        // 预分配空间（避免多次重分配）
        constexpr size_t TEMP_SIZE = 32;
        size_t old_size = out.size();
        out.resize(old_size + TEMP_SIZE);
        
        char* buffer = out.data() + old_size;
        size_t written = 0;
        
        // 根据类型选择最优转换
        if constexpr (std::is_same_v<DecayT, bool>) {
            written = bool_to_chars(buffer, TEMP_SIZE, value);
        }
        else if constexpr (std::is_integral_v<DecayT>) {
            written = int_to_chars(buffer, TEMP_SIZE, value);
        }
        else if constexpr (std::is_floating_point_v<DecayT>) {
            written = float_to_chars(buffer, TEMP_SIZE, static_cast<double>(value));
        }
        else if constexpr (std::is_pointer_v<DecayT>) {
            if (value) {
                written = ptr_to_chars(buffer, TEMP_SIZE, value);
            } else {
                constexpr const char null_str[] = "nullptr";
                written = sizeof(null_str) - 1;
                std::memcpy(buffer, null_str, written);
            }
        }
        else if constexpr (std::is_same_v<DecayT, const char*>) {
            // 字符串特殊处理
            out.resize(old_size);  // 先恢复大小
            if (value) {
                out.append(value);
            } else {
                out.append("(null)");
            }
            return;
        }
        else if constexpr (std::is_same_v<DecayT, std::string> || 
                          std::is_same_v<DecayT, std::string_view>) {
            out.resize(old_size);  // 先恢复大小
            out.append(value);
            return;
        }
        else if constexpr (std::is_same_v<DecayT, char>) {
            out.resize(old_size);
            out.push_back(value);
            return;
        }
        else {
            // 其他类型，尝试 ADL 查找 to_string
            out.resize(old_size);
            using std::to_string;  // 启用 ADL
            out.append(to_string(std::forward<T>(value)));
            return;
        }
        
        // 调整到实际大小
        out.resize(old_size + written);
    }
};

// ---------- 占位符查找优化 ----------
inline size_t find_placeholder(std::string_view str, size_t start = 0) {
    // 优化：减少 find 调用
    for (size_t i = start; i < str.size() - 1; ++i) {
        if (str[i] == '{' && str[i + 1] == '}') {
            return i;
        }
    }
    return std::string_view::npos;
}

// ---------- 格式化核心实现 ----------
template<typename... Args, size_t... Is>
inline void format_impl(std::string& result, 
                       std::string_view fmt,
                       std::index_sequence<Is...>,
                       Args&&... args) {
    size_t pos = 0;
    size_t arg_index = 0;
    
    // 使用折叠表达式处理每个参数
    auto process_arg = [&](auto&& arg) {
        size_t placeholder = find_placeholder(fmt, pos);
        if (placeholder != std::string_view::npos) {
            // 添加占位符前的文本
            if (placeholder > pos) {
                result.append(fmt.data() + pos, placeholder - pos);
            }
            // 添加参数
            Appender<decltype(arg)>::append(result, std::forward<decltype(arg)>(arg));
            pos = placeholder + 2;
        }
        ++arg_index;
    };
    
    (process_arg(std::forward<Args>(args)), ...);
    
    // 添加剩余的格式字符串
    if (pos < fmt.size()) {
        result.append(fmt.data() + pos, fmt.size() - pos);
    }
}

} // namespace detail

// ============================================================
// 主要接口
// ============================================================

// 高性能格式化函数（主接口）
template<typename... Args>
[[nodiscard]] inline std::string FormatMessage(std::string_view fmt, Args&&... args) {
    // 无参数快速路径
    if constexpr (sizeof...(args) == 0) {
        return std::string(fmt);
    }
    
    // 预估结果大小（启发式）
    // 格式字符串长度 + 每个参数平均 8 字符
    size_t estimated_size = fmt.size() + sizeof...(args) * 8;
    
    std::string result;
    result.reserve(estimated_size);
    
    detail::format_impl(result, fmt, 
                       std::index_sequence_for<Args...>{},
                       std::forward<Args>(args)...);
    
    return result;
}

// 兼容旧接口（接受 std::string）
template<typename... Args>
[[nodiscard]] inline std::string FormatMessage(const std::string& fmt, Args&&... args) {
    return FormatMessage(std::string_view(fmt), std::forward<Args>(args)...);
}

} // namespace lyf