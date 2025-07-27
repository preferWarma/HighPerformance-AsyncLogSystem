#include "Singleton.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>

#define getEnv(var, default)                                                   \
  (std::getenv(var) == nullptr ? default : std::getenv(var))

namespace lyf {

using std::string;

// 配置值类型定义
using ConfigValue = std::variant<string, int, double, bool>;

// JSON解析异常
class JsonParseException : public std::runtime_error {
public:
    JsonParseException(const string& message, size_t line = 0, size_t column = 0)
        : std::runtime_error(formatMessage(message, line, column))
        , line_(line), column_(column) {}
    
    size_t getLine() const { return line_; }
    size_t getColumn() const { return column_; }

private:
    size_t line_;
    size_t column_;
    
    static string formatMessage(const string& message, size_t line, size_t column) {
        if (line > 0) {
            return "JSON解析错误(第" + std::to_string(line) + "行,第" + 
                   std::to_string(column) + "列): " + message;
        }
        return "JSON解析错误: " + message;
    }
};

// 改进的JSON解析器
class RobustJsonParser {
private:
    string content_;
    size_t pos_;
    size_t line_;
    size_t column_;
    
public:
    std::unordered_map<string, ConfigValue> parse(const string& json_content) {
        content_ = json_content;
        pos_ = 0;
        line_ = 1;
        column_ = 1;
        
        std::unordered_map<string, ConfigValue> result;
        
        try {
            skipWhitespace();
            if (pos_ >= content_.length()) {
                throw JsonParseException("空JSON内容");
            }
            
            parseObject("", result);
            
            skipWhitespace();
            if (pos_ < content_.length()) {
                throw JsonParseException("JSON内容后存在多余字符");
            }
            
            return result;
            
        } catch (const std::exception& e) {
            throw JsonParseException(e.what(), line_, column_);
        }
    }

private:
    void skipWhitespace() {
        while (pos_ < content_.length()) {
            char c = content_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
            } else if (c == '\n') {
                line_++;
                column_ = 1;
                pos_++;
            } else {
                break;
            }
        }
    }
    
    void advance() {
        if (pos_ < content_.length()) {
            pos_++;
            column_++;
        }
    }
    
    char peek() const {
        return (pos_ < content_.length()) ? content_[pos_] : '\0';
    }
    
    char consume() {
        char c = peek();
        advance();
        return c;
    }
    
    void expect(char expected) {
        char c = consume();
        if (c != expected) {
            throw JsonParseException("期望 '" + string(1, expected) + 
                                   "' 但得到 '" + string(1, c) + "'");
        }
    }
    
    string parseString() {
        expect('"');
        
        string result;
        while (pos_ < content_.length() && peek() != '"') {
            char c = consume();
            
            if (c == '\\') {
                if (pos_ >= content_.length()) {
                    throw JsonParseException("字符串中转义字符不完整");
                }
                
                char escaped = consume();
                switch (escaped) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default:
                        throw JsonParseException("无效的转义字符 \\" + string(1, escaped));
                }
            } else if (c == '\n') {
                throw JsonParseException("字符串中不能包含未转义的换行符");
            } else {
                result += c;
            }
        }
        
        if (peek() != '"') {
            throw JsonParseException("字符串未正确闭合");
        }
        
        expect('"');
        return result;
    }
    
    ConfigValue parseNumber() {
        size_t start = pos_;
        bool is_negative = false;
        bool is_double = false;
        
        // 处理负号
        if (peek() == '-') {
            is_negative = true;
            advance();
        }
        
        // 至少要有一个数字
        if (!std::isdigit(peek())) {
            throw JsonParseException("数字格式无效");
        }
        
        // 解析整数部分
        if (peek() == '0') {
            advance();
            // 0后面只能是小数点或结束
            if (std::isdigit(peek())) {
                throw JsonParseException("数字不能以0开头(除非是0.xxx)");
            }
        } else {
            while (std::isdigit(peek())) {
                advance();
            }
        }
        
        // 解析小数部分
        if (peek() == '.') {
            is_double = true;
            advance();
            
            if (!std::isdigit(peek())) {
                throw JsonParseException("小数点后必须有数字");
            }
            
            while (std::isdigit(peek())) {
                advance();
            }
        }
        
        // 解析指数部分
        if (peek() == 'e' || peek() == 'E') {
            is_double = true;
            advance();
            
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            
            if (!std::isdigit(peek())) {
                throw JsonParseException("指数部分格式无效");
            }
            
            while (std::isdigit(peek())) {
                advance();
            }
        }
        
        string number_str = content_.substr(start, pos_ - start);
        
        try {
            if (is_double) {
                return std::stod(number_str);
            } else {
                return std::stoi(number_str);
            }
        } catch (const std::exception&) {
            throw JsonParseException("数字超出范围: " + number_str);
        }
    }
    
    bool parseBool() {
        if (content_.substr(pos_, 4) == "true") {
            pos_ += 4;
            column_ += 4;
            return true;
        } else if (content_.substr(pos_, 5) == "false") {
            pos_ += 5;
            column_ += 5;
            return false;
        } else {
            throw JsonParseException("无效的布尔值");
        }
    }
    
    void parseNull() {
        if (content_.substr(pos_, 4) == "null") {
            pos_ += 4;
            column_ += 4;
        } else {
            throw JsonParseException("无效的null值");
        }
    }
    
    ConfigValue parseValue() {
        skipWhitespace();
        
        char c = peek();
        switch (c) {
            case '"':
                return parseString();
            case 't':
            case 'f':
                return parseBool();
            case 'n':
                parseNull();
                return string(""); // null转为空字符串
            case '-':
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return parseNumber();
            default:
                throw JsonParseException("无效的值开始字符: " + string(1, c));
        }
    }
    
    void parseObject(const string& prefix, std::unordered_map<string, ConfigValue>& result) {
        expect('{');
        skipWhitespace();
        
        // 处理空对象
        if (peek() == '}') {
            advance();
            return;
        }
        
        bool first = true;
        while (peek() != '}') {
            if (!first) {
                expect(',');
                skipWhitespace();
                
                // 允许尾随逗号
                if (peek() == '}') {
                    break;
                }
            }
            first = false;
            
            // 解析键
            skipWhitespace();
            if (peek() != '"') {
                throw JsonParseException("对象键必须是字符串");
            }
            
            string key = parseString();
            string full_key = prefix.empty() ? key : prefix + "." + key;
            
            skipWhitespace();
            expect(':');
            skipWhitespace();
            
            // 检查值类型
            if (peek() == '{') {
                // 嵌套对象
                parseObject(full_key, result);
            } else if (peek() == '[') {
                // 数组 - 暂不支持，跳过
                throw JsonParseException("暂不支持数组类型: " + full_key);
            } else {
                // 普通值
                result[full_key] = parseValue();
            }
            
            skipWhitespace();
        }
        
        expect('}');
    }
};

class ConfigManagerImpl : public Singleton<ConfigManagerImpl> {
    friend class Singleton<ConfigManagerImpl>;

private:
    std::unordered_map<string, ConfigValue> config_data;
    string config_file_path;
    RobustJsonParser json_parser;

public:
    ConfigManagerImpl(const string &file_path = getEnv("CONF_PATH", "config.json"))
        : config_file_path(file_path) {
        if (std::filesystem::exists(config_file_path)) {
            if (!loadFromFile()) {
                throw std::runtime_error("配置文件加载失败: " + config_file_path);
            }
        } else {
            throw std::runtime_error("配置文件不存在: " + config_file_path);
        }
    }

    // 从文件加载配置
    bool loadFromFile() {
        std::ifstream file(config_file_path);
        if (!file.is_open()) {
            std::cerr << "无法打开配置文件: " << config_file_path << std::endl;
            return false;
        }

        string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
        file.close();

        try {
            config_data = json_parser.parse(content);
            std::cout << "配置文件加载成功: " << config_file_path 
                      << " (共" << config_data.size() << "个配置项)" << std::endl;
            return true;
        } catch (const JsonParseException& e) {
            std::cerr << "JSON解析失败: " << e.what() << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cerr << "配置加载失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 保存配置到文件
    bool saveToFile() {
        std::ofstream file(config_file_path);
        if (!file.is_open()) {
            std::cerr << "无法创建配置文件: " << config_file_path << std::endl;
            return false;
        }

        file << generateNestedJson() << std::endl;
        file.close();
        std::cout << "配置文件保存成功: " << config_file_path << std::endl;
        return true;
    }

    // 验证JSON文件格式
    bool validateJsonFile(const string& file_path = "") {
        string target_file = file_path.empty() ? config_file_path : file_path;
        
        std::ifstream file(target_file);
        if (!file.is_open()) {
            std::cerr << "无法打开文件: " << target_file << std::endl;
            return false;
        }

        string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
        file.close();

        try {
            RobustJsonParser parser;
            parser.parse(content);
            std::cout << "JSON格式验证通过: " << target_file << std::endl;
            return true;
        } catch (const JsonParseException& e) {
            std::cerr << "JSON格式验证失败: " << e.what() << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cerr << "文件验证失败: " << e.what() << std::endl;
            return false;
        }
    }

    // 设置配置项（支持嵌套，使用点号分隔）
    template <typename T> void set(const string &key, const T &value) {
        config_data[key] = value;
    }

    // 获取配置项（带默认值）
    template <typename T> T get(const string &key, const T &default_value = T{}) {
        auto it = config_data.find(key);
        if (it == config_data.end()) {
            return default_value;
        }

        try {
            return std::get<T>(it->second);
        } catch (const std::bad_variant_access &e) {
            std::cerr << "配置项 '" << key << "' 类型不匹配，期望类型: " 
                      << typeid(T).name() << std::endl;
            return default_value;
        }
    }

    // 便利方法：设置组配置
    template <typename T>
    void setGroup(const string &group, const string &key, const T &value) {
        set(group + "." + key, value);
    }

    // 便利方法：获取组配置
    template <typename T>
    T getGroup(const string &group, const string &key,
               const T &default_value = T{}) {
        return get(group + "." + key, default_value);
    }

    // 检查配置项是否存在
    bool has(const string &key) const {
        return config_data.find(key) != config_data.end();
    }

    // 检查组是否存在
    bool hasGroup(const string &group) const {
        auto groups = getGroups();
        return groups.find(group) != groups.end();
    }

    // 删除配置项
    bool remove(const string &key) { 
        return config_data.erase(key) > 0; 
    }

    // 删除整个组
    int removeGroup(const string &group) {
        string prefix = group + ".";
        int count = 0;
        auto it = config_data.begin();
        while (it != config_data.end()) {
            if (it->first.substr(0, prefix.length()) == prefix) {
                it = config_data.erase(it);
                count++;
            } else {
                ++it;
            }
        }
        return count;
    }

    // 获取组下的所有键
    std::vector<string> getGroupKeys(const string &group) const {
        std::vector<string> keys;
        auto group_configs = getGroupConfigs(group);
        for (const auto &pair : group_configs) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    // 获取所有组名
    std::vector<string> getAllGroups() const {
        auto groups_set = getGroups();
        return std::vector<string>(groups_set.begin(), groups_set.end());
    }

    // 清空所有配置
    void clear() { 
        config_data.clear(); 
    }

    // 获取所有配置项的键
    std::vector<string> getKeys() const {
        std::vector<string> keys;
        for (const auto &pair : config_data) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    // 打印所有配置项
    void printAll() const {
        std::cout << "=== 当前配置 ===" << std::endl;

        // 先打印根级别配置
        bool has_root = false;
        for (const auto &pair : config_data) {
            if (pair.first.find('.') == string::npos) {
                if (!has_root) {
                    std::cout << "根配置:" << std::endl;
                    has_root = true;
                }
                std::cout << "  " << pair.first << " = ";
                std::visit([](auto &&arg) { std::cout << arg; }, pair.second);
                std::cout << std::endl;
            }
        }

        // 打印各组配置
        auto groups = getGroups();
        for (const auto &group : groups) {
            std::cout << "\n" << group << ":" << std::endl;
            auto group_configs = getGroupConfigs(group);
            for (const auto &pair : group_configs) {
                std::cout << "  " << pair.first << " = ";
                std::visit([](auto &&arg) { std::cout << arg; }, pair.second);
                std::cout << std::endl;
            }
        }
        std::cout << "=================" << std::endl;
    }

    // 打印指定组的配置
    void printGroup(const string &group) const {
        std::cout << "=== " << group << " 配置 ===" << std::endl;
        auto group_configs = getGroupConfigs(group);
        if (group_configs.empty()) {
            std::cout << "(该组不存在或为空)" << std::endl;
        } else {
            for (const auto &pair : group_configs) {
                std::cout << pair.first << " = ";
                std::visit([](auto &&arg) { std::cout << arg; }, pair.second);
                std::cout << std::endl;
            }
        }
        std::cout << "==================" << std::endl;
    }

    // 获取配置项数量
    size_t size() const { 
        return config_data.size(); 
    }

    // 设置配置文件路径
    void setFilePath(const string &path) { 
        config_file_path = path; 
    }

    // 获取配置文件路径
    const string &getFilePath() const { 
        return config_file_path; 
    }

    // 获取解析统计信息
    void printStatistics() const {
        auto groups = getGroups();
        std::cout << "=== 配置统计 ===" << std::endl;
        std::cout << "总配置项: " << config_data.size() << std::endl;
        std::cout << "配置组数: " << groups.size() << std::endl;
        
        // 统计各种类型的数量
        int string_count = 0, int_count = 0, double_count = 0, bool_count = 0;
        for (const auto& pair : config_data) {
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, string>) {
                    string_count++;
                } else if constexpr (std::is_same_v<T, int>) {
                    int_count++;
                } else if constexpr (std::is_same_v<T, double>) {
                    double_count++;
                } else if constexpr (std::is_same_v<T, bool>) {
                    bool_count++;
                }
            }, pair.second);
        }
        
        std::cout << "类型分布: 字符串(" << string_count << ") 整数(" << int_count 
                  << ") 浮点数(" << double_count << ") 布尔(" << bool_count << ")" << std::endl;
        
        for (const auto& group : groups) {
            auto keys = getGroupKeys(group);
            std::cout << "  " << group << ": " << keys.size() << " 项" << std::endl;
        }
        std::cout << "================" << std::endl;
    }

private:
    // 将ConfigValue转换为JSON字符串
    string valueToJson(const ConfigValue &value) const {
        return std::visit(
            [](auto &&arg) -> string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, string>) {
                    // 转义特殊字符
                    string escaped = arg;
                    // 简单转义，实际应用中可能需要更完整的转义
                    size_t pos = 0;
                    while ((pos = escaped.find('\\', pos)) != string::npos) {
                        escaped.replace(pos, 1, "\\\\");
                        pos += 2;
                    }
                    pos = 0;
                    while ((pos = escaped.find('"', pos)) != string::npos) {
                        escaped.replace(pos, 1, "\\\"");
                        pos += 2;
                    }
                    return "\"" + escaped + "\"";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return arg ? "true" : "false";
                } else {
                    return std::to_string(arg);
                }
            },
            value);
    }

    // 获取所有指定前缀的配置项
    std::unordered_map<string, ConfigValue>
    getGroupConfigs(const string &group) const {
        std::unordered_map<string, ConfigValue> result;
        string prefix = group + ".";

        for (const auto &pair : config_data) {
            if (pair.first.substr(0, prefix.length()) == prefix) {
                string sub_key = pair.first.substr(prefix.length());
                result[sub_key] = pair.second;
            }
        }
        return result;
    }

    // 获取嵌套结构的层级
    std::set<string> getGroups() const {
        std::set<string> groups;
        for (const auto &pair : config_data) {
            size_t dot_pos = pair.first.find('.');
            if (dot_pos != string::npos) {
                groups.insert(pair.first.substr(0, dot_pos));
            }
        }
        return groups;
    }

    // 生成嵌套JSON字符串
    string generateNestedJson(int indent = 0) const {
        string indent_str(indent, ' ');
        std::ostringstream oss;

        // 获取所有顶级组
        auto groups = getGroups();

        // 获取根级别的配置项（不含点的键）
        std::vector<std::pair<string, ConfigValue>> root_configs;
        for (const auto &pair : config_data) {
            if (pair.first.find('.') == string::npos) {
                root_configs.push_back(pair);
            }
        }

        oss << "{\n";

        bool first_item = true;

        // 输出根级别配置
        for (const auto &pair : root_configs) {
            if (!first_item)
                oss << ",\n";
            oss << indent_str << "  \"" << pair.first
                << "\": " << valueToJson(pair.second);
            first_item = false;
        }

        // 输出各个组的嵌套配置
        for (const auto &group : groups) {
            if (!first_item)
                oss << ",\n";
            oss << indent_str << "  \"" << group << "\": {\n";

            auto group_configs = getGroupConfigs(group);
            auto it = group_configs.begin();
            while (it != group_configs.end()) {
                oss << indent_str << "    \"" << it->first
                    << "\": " << valueToJson(it->second);
                ++it;
                if (it != group_configs.end()) {
                    oss << ",";
                }
                oss << "\n";
            }

            oss << indent_str << "  }";
            first_item = false;
        }

        oss << "\n" << indent_str << "}";
        return oss.str();
    }
};

} // namespace lyf