// config_manager_test.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ConfigManager.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <chrono>

using namespace lyf;
using ConfigManager = ConfigManagerImpl;

// 测试夹具类 - 负责测试环境的设置和清理
class ConfigManagerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // 在所有测试开始前创建测试配置文件
        createTestConfigFile();
        
        // 设置环境变量
        #ifdef _WIN32
            _putenv_s("CONF_PATH", "config.json");
        #else
            setenv("CONF_PATH", "config.json", 1);
        #endif
    }

    static void TearDownTestSuite() {
        // 清理测试文件
        try {
            if (std::filesystem::exists("config.json")) {
                std::filesystem::remove("config.json");
            }
        } catch (...) {
            // 忽略清理错误
        }
    }

    void SetUp() override {
        // 每个测试前的设置
        config_ = &ConfigManager::GetInstance();
        // 确保配置文件已加载
        config_->loadFromFile();
    }

    void TearDown() override {
        // 每个测试后的清理
        // 移除测试中添加的配置项
        std::vector<std::string> test_keys = {
            "test_string", "test_int", "test_double", "test_bool",
            "save_test", "new_section.new_key"
        };
        
        for (const auto& key : test_keys) {
            config_->remove(key);
        }
        
        // 移除测试组
        config_->removeGroup("test_group");
        config_->removeGroup("save_group");
        config_->removeGroup("runtime");
        config_->removeGroup("new_section");
    }

    // 创建测试配置文件
    static void createTestConfigFile() {
        std::string config_content = R"({
  "app_name": "TestApplication",
  "version": "1.0.0",
  "debug": true,
  "port": 8080,
  "timeout": 30.5,
  "server": {
    "host": "localhost",
    "port": 3000,
    "ssl_enabled": false,
    "max_connections": 100
  },
  "database": {
    "driver": "postgresql",
    "host": "db.example.com",
    "port": 5432,
    "name": "test_db",
    "connection_pool": {
      "min_size": 5,
      "max_size": 20,
      "timeout": 10.0
    }
  },
  "logging": {
    "level": "INFO",
    "file_path": "/var/log/app.log",
    "max_file_size": 100,
    "rotate": true
  },
  "features": {
    "authentication": true,
    "caching": false,
    "monitoring": true
  }
})";

        std::ofstream file("config.json");
        file << config_content;
        file.close();
    }

protected:
    ConfigManager* config_;
};

// 单例模式测试
class SingletonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 为单例测试创建简单的配置文件
        std::ofstream file("singleton_test.json");
        file << R"({"test": "value"})";
        file.close();
        
        #ifdef _WIN32
            _putenv_s("CONF_PATH", "singleton_test.json");
        #else
            setenv("CONF_PATH", "singleton_test.json", 1);
        #endif
    }
    
    void TearDown() override {
        std::filesystem::remove("singleton_test.json");
    }
};

// ============= 单例模式测试 =============
TEST_F(SingletonTest, InstanceUniqueness) {
    auto& config1 = ConfigManager::GetInstance();
    auto& config2 = ConfigManager::GetInstance();
    
    EXPECT_EQ(&config1, &config2) << "单例实例应该是同一个对象";
}

TEST_F(SingletonTest, ThreadSafety) {
    std::vector<ConfigManager*> instances;
    std::mutex instances_mutex;
    
    std::vector<std::thread> threads;
    const int thread_count = 10;
    
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&]() {
            auto& config = ConfigManager::GetInstance();
            std::lock_guard<std::mutex> lock(instances_mutex);
            instances.push_back(&config);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(instances.size(), thread_count);
    
    for (size_t i = 1; i < instances.size(); ++i) {
        EXPECT_EQ(instances[i], instances[0]) 
            << "多线程环境下所有实例应该相同";
    }
}

// ============= 文件操作测试 =============
TEST_F(ConfigManagerTest, FileLoading) {
    EXPECT_TRUE(config_->has("app_name")) << "配置文件应该正确加载";
    EXPECT_GT(config_->size(), 10) << "配置项数量应该大于10";
}

TEST_F(ConfigManagerTest, FileSaving) {
    // 添加测试配置
    config_->set("save_test", std::string("test_value"));
    config_->setGroup("save_group", "param1", 123);
    config_->setGroup("save_group", "param2", false);
    
    // 保存文件
    EXPECT_TRUE(config_->saveToFile()) << "配置保存应该成功";
    EXPECT_TRUE(std::filesystem::exists(config_->getFilePath())) 
        << "保存的文件应该存在";
    
    // 重新加载验证
    EXPECT_TRUE(config_->loadFromFile()) << "重新加载应该成功";
    EXPECT_EQ(config_->get<std::string>("save_test"), "test_value")
        << "保存的配置应该正确恢复";
    EXPECT_EQ(config_->getGroup<int>("save_group", "param1"), 123)
        << "保存的组配置应该正确恢复";
}

TEST_F(ConfigManagerTest, NonExistentFile) {
    config_->setFilePath("non_existent_file.json");
    EXPECT_FALSE(config_->loadFromFile()) 
        << "加载不存在的文件应该失败";
    
    // 恢复原配置文件
    config_->setFilePath("config.json");
    config_->loadFromFile();
}

// ============= 基本操作测试 =============
TEST_F(ConfigManagerTest, BasicSetGet) {
    // 字符串类型
    config_->set("test_string", std::string("hello"));
    EXPECT_EQ(config_->get<std::string>("test_string"), "hello");
    
    // 整数类型
    config_->set("test_int", 42);
    EXPECT_EQ(config_->get<int>("test_int"), 42);
    
    // 浮点数类型
    config_->set("test_double", 3.14);
    EXPECT_DOUBLE_EQ(config_->get<double>("test_double"), 3.14);
    
    // 布尔类型
    config_->set("test_bool", true);
    EXPECT_TRUE(config_->get<bool>("test_bool"));
}

TEST_F(ConfigManagerTest, DefaultValues) {
    EXPECT_EQ(config_->get<std::string>("non_existent", "default"), "default");
    EXPECT_EQ(config_->get<int>("non_existent", 999), 999);
    EXPECT_DOUBLE_EQ(config_->get<double>("non_existent", 1.23), 1.23);
    EXPECT_FALSE(config_->get<bool>("non_existent", false));
}

TEST_F(ConfigManagerTest, HasMethod) {
    config_->set("test_key", std::string("value"));
    
    EXPECT_TRUE(config_->has("test_key"));
    EXPECT_FALSE(config_->has("non_existent_key"));
    EXPECT_TRUE(config_->has("app_name")); // 从配置文件加载的
}

TEST_F(ConfigManagerTest, RemoveMethod) {
    config_->set("to_remove", std::string("value"));
    EXPECT_TRUE(config_->has("to_remove"));
    
    EXPECT_TRUE(config_->remove("to_remove"));
    EXPECT_FALSE(config_->has("to_remove"));
    
    EXPECT_FALSE(config_->remove("non_existent"));
}

// ============= 数据类型测试 =============
TEST_F(ConfigManagerTest, DataTypeReading) {
    EXPECT_EQ(config_->get<std::string>("app_name"), "TestApplication");
    EXPECT_EQ(config_->get<int>("port"), 8080);
    EXPECT_DOUBLE_EQ(config_->get<double>("timeout"), 30.5);
    EXPECT_TRUE(config_->get<bool>("debug"));
}

TEST_F(ConfigManagerTest, TypeMismatchHandling) {
    // 尝试以错误类型读取配置，应该返回默认值
    EXPECT_EQ(config_->get<int>("app_name", -1), -1);
    EXPECT_EQ(config_->get<std::string>("port", "default"), "default");
}

// ============= 嵌套配置测试 =============
TEST_F(ConfigManagerTest, NestedConfiguration) {
    // 一级嵌套
    EXPECT_EQ(config_->get<std::string>("server.host"), "localhost");
    EXPECT_EQ(config_->get<int>("server.port"), 3000);
    EXPECT_FALSE(config_->get<bool>("server.ssl_enabled"));
    
    // 深层嵌套
    EXPECT_EQ(config_->get<int>("database.connection_pool.min_size"), 5);
    EXPECT_EQ(config_->get<int>("database.connection_pool.max_size"), 20);
    EXPECT_DOUBLE_EQ(config_->get<double>("database.connection_pool.timeout"), 10.0);
}

TEST_F(ConfigManagerTest, NestedConfigurationSetting) {
    config_->set("new_section.new_key", std::string("new_value"));
    EXPECT_EQ(config_->get<std::string>("new_section.new_key"), "new_value");
    
    config_->set("deep.nested.value", 123);
    EXPECT_EQ(config_->get<int>("deep.nested.value"), 123);
}

// ============= 组操作测试 =============
TEST_F(ConfigManagerTest, GroupOperations) {
    // 组存在性检查
    EXPECT_TRUE(config_->hasGroup("server"));
    EXPECT_TRUE(config_->hasGroup("database"));
    EXPECT_FALSE(config_->hasGroup("non_existent_group"));
    
    // 设置组配置
    config_->setGroup("test_group", "key1", std::string("value1"));
    config_->setGroup("test_group", "key2", 100);
    config_->setGroup("test_group", "key3", true);
    
    // 获取组配置
    EXPECT_EQ(config_->getGroup<std::string>("test_group", "key1"), "value1");
    EXPECT_EQ(config_->getGroup<int>("test_group", "key2"), 100);
    EXPECT_TRUE(config_->getGroup<bool>("test_group", "key3"));
}

TEST_F(ConfigManagerTest, GroupKeys) {
    auto server_keys = config_->getGroupKeys("server");
    EXPECT_GE(server_keys.size(), 3) << "server组应该至少有3个键";
    
    auto all_groups = config_->getAllGroups();
    EXPECT_GE(all_groups.size(), 4) << "应该至少有4个配置组";
    
    // 检查特定组是否在列表中
    EXPECT_THAT(all_groups, ::testing::Contains("server"));
    EXPECT_THAT(all_groups, ::testing::Contains("database"));
}

TEST_F(ConfigManagerTest, GroupRemoval) {
    // 创建测试组
    config_->setGroup("temp_group", "key1", std::string("value1"));
    config_->setGroup("temp_group", "key2", 42);
    config_->setGroup("temp_group", "key3", true);
    
    EXPECT_TRUE(config_->hasGroup("temp_group"));
    
    // 删除组
    int removed_count = config_->removeGroup("temp_group");
    EXPECT_EQ(removed_count, 3);
    EXPECT_FALSE(config_->hasGroup("temp_group"));
}

// ============= 验证功能测试 =============
TEST_F(ConfigManagerTest, ConfigValidation) {
    // 检查必需配置
    EXPECT_TRUE(config_->has("app_name"));
    EXPECT_TRUE(config_->has("database.driver"));
    
    // 数值范围验证
    int port_value = config_->get<int>("port");
    EXPECT_GT(port_value, 0);
    EXPECT_LE(port_value, 65535);
    
    int max_connections = config_->get<int>("server.max_connections");
    EXPECT_GT(max_connections, 0);
    
    // 字符串非空验证
    std::string driver = config_->get<std::string>("database.driver");
    EXPECT_FALSE(driver.empty());
}

// ============= 性能测试 =============
class ConfigManagerPerformanceTest : public ConfigManagerTest {
protected:
    static constexpr int ITERATIONS = 1000; // 减少迭代次数以加快测试
};

TEST_F(ConfigManagerPerformanceTest, BulkSetOperations) {
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ITERATIONS; ++i) {
        config_->set("perf_test_" + std::to_string(i), i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 1000) 
        << "大量设置操作应该在1秒内完成，实际耗时: " << duration.count() << "ms";
    
    // 清理
    for (int i = 0; i < ITERATIONS; ++i) {
        config_->remove("perf_test_" + std::to_string(i));
    }
}

TEST_F(ConfigManagerPerformanceTest, BulkGetOperations) {
    // 先设置测试数据
    for (int i = 0; i < ITERATIONS; ++i) {
        config_->set("perf_test_" + std::to_string(i), i);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int sum = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        sum += config_->get<int>("perf_test_" + std::to_string(i), 0);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 1000)
        << "大量读取操作应该在1秒内完成，实际耗时: " << duration.count() << "ms";
    
    // 验证读取正确性
    EXPECT_EQ(sum, (ITERATIONS - 1) * ITERATIONS / 2);
    
    // 清理
    for (int i = 0; i < ITERATIONS; ++i) {
        config_->remove("perf_test_" + std::to_string(i));
    }
}

// ============= 边界条件测试 =============
TEST_F(ConfigManagerTest, EdgeCases) {
    // 空键测试
    config_->set("", std::string("empty_key"));
    EXPECT_TRUE(config_->has(""));
    EXPECT_EQ(config_->get<std::string>(""), "empty_key");
    
    // 特殊字符键测试
    config_->set("key-with.special_chars", 42);
    EXPECT_EQ(config_->get<int>("key-with.special_chars"), 42);
    
    // 长键名测试
    std::string long_key(1000, 'a');
    config_->set(long_key, std::string("long_key_value"));
    EXPECT_EQ(config_->get<std::string>(long_key), "long_key_value");
    
    // 清理
    config_->remove("");
    config_->remove("key-with.special_chars");
    config_->remove(long_key);
}

// ============= 辅助功能测试 =============
TEST_F(ConfigManagerTest, UtilityMethods) {
    size_t initial_size = config_->size();
    
    config_->set("utility_test", std::string("test"));
    EXPECT_EQ(config_->size(), initial_size + 1);
    
    auto keys = config_->getKeys();
    EXPECT_THAT(keys, ::testing::Contains("utility_test"));
    
    config_->clear();
    EXPECT_EQ(config_->size(), 0);
    
    // 重新加载配置文件
    config_->loadFromFile();
    EXPECT_GT(config_->size(), 0);
}

// ============= 参数化测试示例 =============
class DataTypeTest : public ConfigManagerTest, 
                    public ::testing::WithParamInterface<std::pair<std::string, std::string>> {
};

TEST_P(DataTypeTest, ReadConfiguredValues) {
    auto param = GetParam();
    std::string key = param.first;
    std::string expected = param.second;
    
    EXPECT_EQ(config_->get<std::string>(key), expected);
}

INSTANTIATE_TEST_SUITE_P(
    ConfiguredStringValues,
    DataTypeTest,
    ::testing::Values(
        std::make_pair("app_name", "TestApplication"),
        std::make_pair("version", "1.0.0"),
        std::make_pair("database.driver", "postgresql"),
        std::make_pair("logging.level", "INFO")
    )
);

// ============= 主函数 =============
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}