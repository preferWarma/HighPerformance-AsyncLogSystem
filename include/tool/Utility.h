#include "Singleton.h"
#include "third/json.hpp" // nlohmann json库

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef LYF_lyf_Internal_LOG
#include "FastFormater.h"
#include <iostream>
#if _LIBCPP_STD_VER >= 20
#define lyf_Internal_LOG(fmt, ...)                                             \
  std::cout << FormatMessage<fmt>(__VA_ARGS__) << std::endl;
#else
#define lyf_Internal_LOG(fmt, ...)                                             \
  std::cout << FormatMessage(fmt, ##__VA_ARGS__) << std::endl;
#endif
#else
#define lyf_Internal_LOG(fmt, ...) ((void)0)
#endif

namespace lyf {
namespace fs = std::filesystem;
namespace chrono = std::chrono;

using std::string, std::vector, std::shared_ptr;
using json = nlohmann::json;
using system_clock = chrono::system_clock;

class JsonHelper : public Singleton<JsonHelper> {
  friend class Singleton<JsonHelper>;

private:
  json config_data;
  string config_file_path;

public:
  JsonHelper(const string &file_path = "config.json")
      : config_file_path(file_path) {
    if (fs::exists(config_file_path)) {
      if (!LoadFromFile()) {
        throw std::runtime_error("配置文件加载失败: " + config_file_path);
      }
    } else {
      // 如果文件不存在，创建一个空的json对象
      lyf_Internal_LOG(">>>>>>配置文件不存在，将创建一个空的配置文件: {}\n",
                       config_file_path.c_str());
      config_data = json::object();
    }
  }

public: // 加载和保存操作
  // 从文件加载配置
  bool LoadFromFile() {
    std::ifstream file(config_file_path);
    if (!file.is_open()) {
      lyf_Internal_LOG("无法打开配置文件: {}\n", config_file_path.c_str());
      return false;
    }

    try {
      config_data = json::parse(file);
      size_t top_level_keys = config_data.size();
      size_t total_properties = CountLeafProperties(config_data);
      lyf_Internal_LOG(">>>>>>配置文件({})加载成功: 共{}个配置, {}个配置属性\n",
                       fs::absolute(config_file_path).c_str(), top_level_keys,
                       total_properties);
      return true;
    } catch (const json::parse_error &e) {
      lyf_Internal_LOG("JSON解析失败: {}\n", e.what());
      return false;
    } catch (const std::exception &e) {
      lyf_Internal_LOG("配置加载失败: {}\n", e.what());
      return false;
    }
  }

  // 保存配置到文件
  bool SaveToFile() {
    std::ofstream file(config_file_path);
    if (!file.is_open()) {
      lyf_Internal_LOG("无法创建配置文件: {}\n", config_file_path.c_str());
      return false;
    }

    file << config_data.dump(4) << std::endl;
    file.close();
    lyf_Internal_LOG("配置文件保存成功: {}, 共{}个配置, {}个配置属性\n",
                     fs::absolute(config_file_path).c_str(), config_data.size(),
                     CountLeafProperties(config_data));
    return true;
  }

  // 设置配置文件路径
  void SetFilePath(const string &path) { config_file_path = path; }

  // 获取配置文件路径
  const string &getFilePath() const { return config_file_path; }

  // 重新加载配置文件
  bool ReloadFromFile(const string other_path = "") {
    if (fs::exists(other_path) &&
        fs::absolute(other_path) != fs::absolute(config_file_path)) {
      SetFilePath(other_path);
      lyf_Internal_LOG(">>>>>>配置文件已更新为: {}\n",
                       fs::absolute(config_file_path).c_str());
    }
    return LoadFromFile();
  }

public: // 配置项操作
  // 设置配置项（支持嵌套，使用点号分隔）
  template <typename T> void Set(const string &key, const T &value) {
    config_data[json::json_pointer(KeyToPtr(key))] = value;
  }

  // 获取配置项（带默认值）
  template <typename T> T Get(const string &key, const T &default_value = T{}) {
    try {
      return config_data.at(json::json_pointer(KeyToPtr(key))).get<T>();
    } catch (const json::out_of_range &) { // 键不存在,返回默认值
      return default_value;
    } catch (const json::type_error &) {
      lyf_Internal_LOG("配置项 '{}' 类型不匹配，期望类型: {}\n", key.c_str(),
                       typeid(T).name());
      return default_value;
    }
  }

  // 便利方法：设置组配置
  template <typename T>
  void SetWithGroup(const string &group, const string &key, const T &value) {
    Set(group + "." + key, value);
  }

  // 便利方法：获取组配置
  template <typename T>
  T GetWithGroup(const string &group, const string &key,
                 const T &default_value = T{}) {
    return Get(group + "." + key, default_value);
  }

  // 检查配置项是否存在
  bool Has(const string &key) const {
    return !config_data.value(json::json_pointer(KeyToPtr(key)), json())
                .is_null();
  }

  // 检查组是否存在
  bool HasGroup(const string &group) const { return Has(group); }

  // 删除配置项
  bool Remove(const string &key) {
    try {
      size_t last_dot = key.find_last_of('.');
      if (last_dot == string::npos) {
        // Top-level key
        return config_data.erase(key) > 0;
      }

      string parent_key = key.substr(0, last_dot);
      string child_key = key.substr(last_dot + 1);

      json::json_pointer parent_ptr(KeyToPtr(parent_key));
      json &parent_obj = config_data.at(parent_ptr);

      if (parent_obj.is_object()) {
        return parent_obj.erase(child_key) > 0;
      }
      return false;

    } catch (const json::out_of_range &) {
      return false; // Parent key doesn't exist
    }
  }

  // 删除整个组
  bool RemoveGroup(const string &group) { return Remove(group); }

  // 获取组下的所有键
  vector<string> GetGroupKeys(const string &group) const {
    vector<string> keys;
    try {
      auto group_obj = config_data.at(json::json_pointer(KeyToPtr(group)));
      if (group_obj.is_object()) {
        for (auto &el : group_obj.items()) {
          keys.push_back(el.key());
        }
      }
    } catch (const json::out_of_range &) {
      // group not found
    }
    return keys;
  }

  // 获取所有组名
  vector<string> GetAllGroups() const {
    vector<string> groups;
    if (config_data.is_object()) {
      for (auto &el : config_data.items()) {
        if (el.value().is_object()) {
          groups.push_back(el.key());
        }
      }
    }
    return groups;
  }

  // 清空所有配置
  void Clear() { config_data.clear(); }

  // 获取所有配置项的键
  vector<string> GetAllKeys() const {
    vector<string> keys;
    if (config_data.is_object()) {
      for (auto &el : config_data.items()) {
        keys.push_back(el.key());
      }
    }
    return keys;
  }

public: // 打印操作
  // 打印所有配置项
  void PrintAllConfig() const {
    std::cout << "=== 当前配置 ===" << std::endl;
    std::cout << config_data.dump(4) << std::endl;
    std::cout << "=================" << std::endl;
  }

  // 打印指定组的配置
  void PrintGroup(const string &group) const {
    std::cout << "=== " << group << " 配置 ===" << std::endl;
    try {
      auto group_obj = config_data.at(json::json_pointer(KeyToPtr(group)));
      std::cout << group_obj.dump(4) << std::endl;
    } catch (const json::out_of_range &) {
      std::cout << "(该组不存在或为空)" << std::endl;
    }
    std::cout << "==================" << std::endl;
  }

  // 获取解析统计信息
  void printStatistics() const {
    std::cout << "=== 配置统计 ===\n"
              << "总配置项: " << Size() << "\n"
              << "总配置属性: " << PropertySize() << "\n"
              << "=================" << std::endl;
  }

public: // 统计操作
  // 获取配置项数量
  size_t Size() const { return config_data.size(); }

  // 获取配置属性数量
  size_t PropertySize() const { return CountLeafProperties(config_data); }

private:
  size_t CountLeafProperties(const json &j) const {
    size_t count = 0;
    if (j.is_object()) {
      for (auto &el : j.items()) {
        if (el.value().is_primitive()) {
          count++;
        } else {
          count += CountLeafProperties(el.value());
        }
      }
    }
    return count;
  }

  // 辅助函数：将点号分隔的键转换为JSON指针
  // example: "a.b.c" -> "/a/b/c"
  string KeyToPtr(const string &key) const {
    string ptr = "/";
    for (char c : key) {
      if (c == '.') {
        ptr += '/';
      } else {
        ptr += c;
      }
    }
    return ptr;
  }
};

/// @brief 获取当前时间戳
/// @return 当前时间戳
/// @note 单位为毫秒
inline int64_t GetCurrentTimeStamp() {
  return duration_cast<chrono::milliseconds>(
             system_clock::now().time_since_epoch())
      .count();
}

/// @brief 获取当前时间的格式化字符串
/// @param format 时间格式字符串, 默认为"%Y-%m-%d %H:%M:%S"
/// @return 格式化后的时间字符串
inline string GetCurrentTime(const string &format = "%Y-%m-%d %H:%M:%S") {
  auto now = system_clock::now();
  auto time_t = system_clock::to_time_t(now);
  std::stringstream ss;
  // 线程安全的时间格式化函数
  ss << std::put_time(std::localtime(&time_t), format.c_str());
  return ss.str();
}

/// @brief 格式化时间点为字符串
/// @param timePoint 时间点
/// @param format 时间格式字符串, 默认为"%Y-%m-%d %H:%M:%S"
/// @return 格式化后的时间字符串
inline string FormatTime(const system_clock::time_point &timePoint,
                         const string &format = "%Y-%m-%d %H:%M:%S") {
  std::time_t time = system_clock::to_time_t(timePoint);
  char buf[1024];
  strftime(buf, sizeof(buf), format.c_str(), localtime(&time));
  return buf;
}

/// @brief RAII 确保标志被重置
class FlagGuard {
public:
  FlagGuard(std::atomic<bool> &flag) : _flag(flag) {}
  ~FlagGuard() { _flag.store(false); }

private:
  std::atomic<bool> &_flag;
};

/// @brief 创建日志目录
/// @param path 日志文件路径
/// @return 创建成功返回true, 否则返回false
inline bool CreateLogDirectory(const string &path) {
  try {
    auto dir = std::filesystem::path(path).parent_path();
    if (!std::filesystem::exists(dir)) {
      std::filesystem::create_directories(dir);
    }
    return true;
  } catch (const std::exception &e) {
    lyf_Internal_LOG("Failed to create log directory: {}\n", e.what());
    return false;
  }
}

/// @brief 获取文件上一次修改时间
/// @param filePath 文件路径
/// @return 文件上一次修改时间
inline std::filesystem::file_time_type
getFileLastWriteTime(const string &filePath) {
  try {
    return std::filesystem::last_write_time(filePath);
  } catch (const std::exception &e) {
    lyf_Internal_LOG("Failed to get last write time: {}\n", e.what());
    return std::filesystem::file_time_type::min();
  }
}

[[nodiscard]] constexpr bool starts_with(std::string_view sv,
                                         std::string_view prefix) noexcept {
  return sv.size() >= prefix.size() &&
         sv.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] constexpr bool ends_with(std::string_view sv,
                                       std::string_view suffix) noexcept {
  return sv.size() >= suffix.size() &&
         sv.compare(sv.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace lyf