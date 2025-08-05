#include <gtest/gtest.h>
#include "JsonHelper.h"
#include <fstream>
#include <filesystem>

using namespace lyf;

class JsonHelperTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_config_path = "test_config.json";
        if (std::filesystem::exists(test_config_path)) {
            std::filesystem::remove(test_config_path);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(test_config_path)) {
            std::filesystem::remove(test_config_path);
        }
    }

    string test_config_path;
};

// 测试构造函数 - 文件不存在时创建空配置
TEST_F(JsonHelperTest, Constructor_CreateNewConfig) {
    ASSERT_FALSE(std::filesystem::exists(test_config_path));
    
    JsonHelper helper(test_config_path);
    helper.SaveToFile();
    EXPECT_TRUE(std::filesystem::exists(test_config_path));
    EXPECT_EQ(helper.Size(), 0);
}

// 测试构造函数 - 加载现有配置文件
TEST_F(JsonHelperTest, Constructor_LoadExistingConfig) {
    {
        json j;
        j["test_key"] = "test_value";
        j["nested"]["key"] = 42;
        std::ofstream file(test_config_path);
        file << j.dump(4);
    }

    JsonHelper helper(test_config_path);
    EXPECT_EQ(helper.Size(), 2);
    EXPECT_EQ(helper.Get<string>("test_key"), "test_value");
    EXPECT_EQ(helper.Get<int>("nested.key"), 42);
}

// 测试Set和Get方法
TEST_F(JsonHelperTest, SetAndGetBasicTypes) {
    JsonHelper helper(test_config_path);

    // 测试字符串
    helper.Set("string_key", "hello world");
    EXPECT_EQ(helper.Get<string>("string_key"), "hello world");

    // 测试整数
    helper.Set("int_key", 123);
    EXPECT_EQ(helper.Get<int>("int_key"), 123);

    // 测试浮点数
    helper.Set("float_key", 3.14);
    EXPECT_EQ(helper.Get<double>("float_key"), 3.14);

    // 测试布尔值
    helper.Set("bool_key", true);
    EXPECT_TRUE(helper.Get<bool>("bool_key"));

    // 测试默认值
    EXPECT_EQ(helper.Get<string>("nonexistent_key", "default"), "default");
    EXPECT_EQ(helper.Get<int>("nonexistent_key", 999), 999);
}

// 测试嵌套配置
TEST_F(JsonHelperTest, NestedConfiguration) {
    JsonHelper helper(test_config_path);

    helper.Set("database.host", "localhost");
    helper.Set("database.port", 5432);
    helper.Set("database.ssl", true);

    EXPECT_EQ(helper.Get<string>("database.host"), "localhost");
    EXPECT_EQ(helper.Get<int>("database.port"), 5432);
    EXPECT_TRUE(helper.Get<bool>("database.ssl"));

    // 测试SetWithGroup和GetWithGroup
    helper.SetWithGroup("cache", "ttl", 3600);
    helper.SetWithGroup("cache", "max_size", 1024);

    EXPECT_EQ(helper.GetWithGroup<int>("cache", "ttl"), 3600);
    EXPECT_EQ(helper.GetWithGroup<int>("cache", "max_size"), 1024);
}

// 测试Has和HasGroup方法
TEST_F(JsonHelperTest, HasAndHasGroup) {
    JsonHelper helper(test_config_path);

    helper.Set("level1.level2.key", "value");
    helper.Set("group1.subgroup.key", "value");

    EXPECT_TRUE(helper.Has("level1"));
    EXPECT_TRUE(helper.Has("level1.level2"));
    EXPECT_TRUE(helper.Has("level1.level2.key"));
    EXPECT_FALSE(helper.Has("nonexistent"));

    EXPECT_TRUE(helper.HasGroup("level1"));
    EXPECT_TRUE(helper.HasGroup("group1"));
    EXPECT_FALSE(helper.HasGroup("nonexistent_group"));
}

// 测试Remove和RemoveGroup方法
TEST_F(JsonHelperTest, RemoveAndRemoveGroup) {
    JsonHelper helper(test_config_path);

    helper.Set("key1", "value1");
    helper.Set("group1.key", "value2");
    helper.Set("group1.subgroup.key", "value3");

    EXPECT_TRUE(helper.Remove("key1"));
    EXPECT_FALSE(helper.Has("key1"));

    EXPECT_TRUE(helper.Remove("group1.subgroup.key"));
    EXPECT_FALSE(helper.Has("group1.subgroup.key"));

    EXPECT_TRUE(helper.RemoveGroup("group1"));
    EXPECT_FALSE(helper.HasGroup("group1"));

    EXPECT_FALSE(helper.Remove("nonexistent"));
    EXPECT_FALSE(helper.RemoveGroup("nonexistent"));
}

// 测试GetGroupKeys方法
TEST_F(JsonHelperTest, GetGroupKeys) {
    JsonHelper helper(test_config_path);

    helper.Set("app.name", "MyApp");
    helper.Set("app.version", "1.0");
    helper.Set("app.debug", true);

    vector<string> keys = helper.GetGroupKeys("app");
    EXPECT_EQ(keys.size(), 3);
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "name") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "version") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "debug") != keys.end());

    vector<string> nonexistent_keys = helper.GetGroupKeys("nonexistent");
    EXPECT_TRUE(nonexistent_keys.empty());
}

// 测试GetAllGroups方法
TEST_F(JsonHelperTest, GetAllGroups) {
    JsonHelper helper(test_config_path);

    helper.Set("app.name", "MyApp");
    helper.Set("database.host", "localhost");
    helper.Set("cache.ttl", 3600);

    vector<string> groups = helper.GetAllGroups();
    EXPECT_EQ(groups.size(), 3);
    EXPECT_TRUE(std::find(groups.begin(), groups.end(), "app") != groups.end());
    EXPECT_TRUE(std::find(groups.begin(), groups.end(), "database") != groups.end());
    EXPECT_TRUE(std::find(groups.begin(), groups.end(), "cache") != groups.end());
}

// 测试SaveToFile和LoadFromFile方法
TEST_F(JsonHelperTest, SaveAndLoadFile) {
    JsonHelper helper(test_config_path);

    helper.Set("test_data", "save_load_test");
    helper.Set("nested.value", 42);

    EXPECT_TRUE(helper.SaveToFile());

    // 创建新的helper实例来验证加载
    JsonHelper new_helper(test_config_path);
    EXPECT_EQ(new_helper.Get<string>("test_data"), "save_load_test");
    EXPECT_EQ(new_helper.Get<int>("nested.value"), 42);
}

// 测试ReloadFromFile方法
TEST_F(JsonHelperTest, ReloadFromFile) {
    JsonHelper helper(test_config_path);
    helper.Set("initial", "data");

    // 修改文件内容
    {
        json j;
        j["reloaded"] = "new_data";
        j["number"] = 100;
        std::ofstream file(test_config_path);
        file << j.dump(4);
    }

    EXPECT_TRUE(helper.ReloadFromFile());
    EXPECT_FALSE(helper.Has("initial"));
    EXPECT_EQ(helper.Get<string>("reloaded"), "new_data");
    EXPECT_EQ(helper.Get<int>("number"), 100);
}

// 测试SetFilePath方法
TEST_F(JsonHelperTest, SetFilePath) {
    JsonHelper helper(test_config_path);
    helper.Set("key", "value");

    string new_path = "new_test_config.json";
    helper.SetFilePath(new_path);
    EXPECT_EQ(helper.getFilePath(), new_path);

    if (std::filesystem::exists(new_path)) {
        std::filesystem::remove(new_path);
    }
}

// 测试Clear方法
TEST_F(JsonHelperTest, Clear) {
    JsonHelper helper(test_config_path);

    helper.Set("key1", "value1");
    helper.Set("group.key", "value2");
    EXPECT_GT(helper.Size(), 0);

    helper.Clear();
    EXPECT_EQ(helper.Size(), 0);
}

// 测试Size和PropertySize方法
TEST_F(JsonHelperTest, SizeAndPropertySize) {
    JsonHelper helper(test_config_path);

    EXPECT_EQ(helper.Size(), 0);
    EXPECT_EQ(helper.PropertySize(), 0);

    helper.Set("key1", "value1");
    helper.Set("group.key2", "value2");
    helper.Set("group.subgroup.key3", "value3");

    EXPECT_EQ(helper.Size(), 2); // key1, group
    EXPECT_EQ(helper.PropertySize(), 3); // key1, key2, key3
}

// 测试类型转换错误处理
TEST_F(JsonHelperTest, TypeConversionError) {
    JsonHelper helper(test_config_path);

    helper.Set("string_value", "not_a_number");
    
    // 尝试将字符串转换为整数，应该返回默认值
    EXPECT_EQ(helper.Get<int>("string_value", 42), 42);
}

// 测试异常处理 - 无效JSON文件
TEST_F(JsonHelperTest, InvalidJsonFile) {
    {
        std::ofstream file(test_config_path);
        file << "{ invalid json content }";
    }

    // 构造函数会抛出异常
    EXPECT_THROW(JsonHelper helper(test_config_path), std::runtime_error);
}

// 测试数组配置
TEST_F(JsonHelperTest, ArrayConfiguration) {
    JsonHelper helper(test_config_path);

    vector<int> numbers = {1, 2, 3, 4, 5};
    helper.Set("numbers", numbers);

    vector<int> loaded_numbers = helper.Get<vector<int>>("numbers");
    EXPECT_EQ(loaded_numbers.size(), 5);
    EXPECT_EQ(loaded_numbers[0], 1);
    EXPECT_EQ(loaded_numbers[4], 5);
}

// 测试复杂嵌套结构
TEST_F(JsonHelperTest, ComplexNestedStructure) {
    JsonHelper helper(test_config_path);

    json complex = {
        {"server", {
            {"host", "127.0.0.1"},
            {"port", 8080},
            {"ssl", {
                {"enabled", true},
                {"cert_path", "/path/to/cert"}
            }}
        }},
        {"features", {
            {"logging", true},
            {"debug", false}
        }}
    };

    helper.Set("config", complex);

    EXPECT_EQ(helper.Get<string>("config.server.host"), "127.0.0.1");
    EXPECT_EQ(helper.Get<int>("config.server.port"), 8080);
    EXPECT_TRUE(helper.Get<bool>("config.server.ssl.enabled"));
    EXPECT_EQ(helper.Get<string>("config.server.ssl.cert_path"), "/path/to/cert");
    EXPECT_TRUE(helper.Get<bool>("config.features.logging"));
    EXPECT_FALSE(helper.Get<bool>("config.features.debug"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
