/*
 * Copyright (c) 2026 by Rockchip Electronics Co., Ltd. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yaml_parse.h"

// 测试辅助宏
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("❌ 测试失败: %s\n", message); \
            return -1; \
        } else { \
            printf("✅ %s\n", message); \
        } \
    } while(0)

// 测试基本的嵌套对象
int test_nested_objects(YamlDocument *doc) {
    printf("\n===== 测试 1: 嵌套对象 =====\n");

    // 测试获取嵌套字符串值
    const char *name = yaml_get_string(doc, "server.name", "default");
    TEST_ASSERT(strcmp(name, "MyServer") == 0, "获取嵌套字符串值");

    // 测试获取嵌套整数值
    int64_t port = yaml_get_int(doc, "server.port", 0);
    TEST_ASSERT(port == 8080, "获取嵌套整数值");

    // 测试获取嵌套布尔值
    int enabled = yaml_get_bool(doc, "server.enabled", 0);
    TEST_ASSERT(enabled == 1, "获取嵌套布尔值");

    // 测试获取嵌套浮点数
    double timeout = yaml_get_float(doc, "server.timeout", 0.0);
    TEST_ASSERT(timeout == 30.5, "获取嵌套浮点数值");

    // 测试多层嵌套（三层）
    const char *db_driver = yaml_get_string(doc, "database.connection.driver", "none");
    TEST_ASSERT(strcmp(db_driver, "mysql") == 0, "获取三层嵌套值");

    int64_t db_port = yaml_get_int(doc, "database.connection.port", 0);
    TEST_ASSERT(db_port == 3306, "获取三层嵌套整数");

    return 0;
}

// 测试数组/列表
int test_arrays(YamlDocument *doc) {
    printf("\n===== 测试 2: 数组/列表 =====\n");

    // 测试获取数组长度
    int server_count = yaml_get_count(doc, "servers");
    TEST_ASSERT(server_count == 3, "获取服务器列表长度");

    // 测试访问数组元素
    YamlNode *server0 = yaml_get_list_item(doc, "servers", 0);
    TEST_ASSERT(server0 != NULL, "获取数组第 1 个元素");

    YamlNode *server1 = yaml_get_list_item(doc, "servers", 1);
    TEST_ASSERT(server1 != NULL, "获取数组第 2 个元素");

    YamlNode *server2 = yaml_get_list_item(doc, "servers", 2);
    TEST_ASSERT(server2 != NULL, "获取数组第 3 个元素");

    // 测试获取数组中的嵌套对象字段
    const char *host1 = yaml_get_string(doc, "servers", 0); // 注意：这个不会工作，因为不支持这种语法
    // 我们需要直接从节点获取
    if (server0 && server0->type == YAML_TYPE_STRING) {
        printf("✅ 服务器 1: %s\n", server0->value.string_val);
    }

    // 测试用户列表（带键值对的列表项）
    int user_count = yaml_get_count(doc, "users");
    TEST_ASSERT(user_count == 2, "获取用户列表长度");

    YamlNode *user0 = yaml_get_list_item(doc, "users", 0);
    TEST_ASSERT(user0 != NULL && strcmp(user0->key, "name") == 0, "获取用户列表第 1 个元素");

    // 测试嵌套数组
    int tags_count = yaml_get_count(doc, "tags");
    TEST_ASSERT(tags_count == 4, "获取简单标签列表长度");

    return 0;
}

// 测试默认值
int test_default_values(YamlDocument *doc) {
    printf("\n===== 测试 3: 默认值处理 =====\n");

    // 测试不存在的路径返回默认值
    const char *missing_str = yaml_get_string(doc, "nonexistent.path", "default_value");
    TEST_ASSERT(strcmp(missing_str, "default_value") == 0, "不存在的字符串路径返回默认值");

    int64_t missing_int = yaml_get_int(doc, "missing.number", 999);
    TEST_ASSERT(missing_int == 999, "不存在的整数路径返回默认值");

    int missing_bool = yaml_get_bool(doc, "missing.flag", 1);
    TEST_ASSERT(missing_bool == 1, "不存在的布尔路径返回默认值");

    double missing_float = yaml_get_float(doc, "missing.value", 3.14);
    TEST_ASSERT(missing_float == 3.14, "不存在的浮点路径返回默认值");

    return 0;
}

// 测试类型转换
int test_type_conversion(YamlDocument *doc) {
    printf("\n===== 测试 4: 类型转换 =====\n");

    // 字符串转整数
    int64_t str_port = yaml_get_int(doc, "server.port", 0);
    TEST_ASSERT(str_port == 8080, "从整数类型获取整数值");

    // 布尔值转整数
    int enabled_int = yaml_get_bool(doc, "server.enabled", 0);
    TEST_ASSERT(enabled_int == 1, "从布尔类型获取布尔值");

    // 整数转浮点
    double port_float = yaml_get_float(doc, "server.port", 0.0);
    TEST_ASSERT(port_float == 8080.0, "从整数类型获取浮点值");

    return 0;
}

// 演示遍历数组
void demo_array_iteration(YamlDocument *doc) {
    printf("\n===== 演示: 遍历数组 =====\n");

    int count = yaml_get_count(doc, "servers");
    printf("服务器列表 (%d 个):\n", count);

    for (int i = 0; i < count; i++) {
        YamlNode *item = yaml_get_list_item(doc, "servers", i);
        if (item) {
            printf("  [%d] ", i);
            if (item->type == YAML_TYPE_STRING) {
                printf("%s\n", item->value.string_val);
            } else if (item->type == YAML_TYPE_INT) {
                printf("%lld\n", (long long)item->value.int_val);
            }
        }
    }

    printf("\n用户列表 (%d 个):\n", yaml_get_count(doc, "users"));
    for (int i = 0; i < yaml_get_count(doc, "users"); i++) {
        YamlNode *item = yaml_get_list_item(doc, "users", i);
        if (item && item->key[0] != '\0') {
            printf("  [%d] %s: %s\n", i, item->key, item->value.string_val);
        }
    }
}

// 主测试函数
int main(int argc, char *argv[]) {
    const char *filename = "config.yaml";

    // 允许从命令行指定配置文件
    if (argc > 1) {
        filename = argv[1];
    }

    printf("使用配置文件: %s\n", filename);

    // 1. 初始化文档
    YamlDocument doc;
    yaml_doc_init(&doc);

    // 2. 解析文件
    int ret = yaml_parse_file(&doc, filename);
    if (ret != 0) {
        printf("❌ YAML 解析失败！\n");
        yaml_doc_cleanup(&doc);
        return 1;
    }

    printf("✅ YAML 解析成功！\n");

    // 3. 打印完整树形结构
    yaml_print_tree(&doc);

    // 4. 运行测试套件
    int failed = 0;
    failed |= test_nested_objects(&doc);
    failed |= test_arrays(&doc);
    failed |= test_default_values(&doc);
    failed |= test_type_conversion(&doc);

    // 5. 演示数组遍历
    demo_array_iteration(&doc);

    // 6. 清理资源
    yaml_doc_cleanup(&doc);

    // 7. 返回测试结果
    printf("\n===== 测试总结 =====\n");
    if (failed) {
        printf("❌ 部分测试失败\n");
        return 1;
    } else {
        printf("✅ 所有测试通过！\n");
        return 0;
    }
}
