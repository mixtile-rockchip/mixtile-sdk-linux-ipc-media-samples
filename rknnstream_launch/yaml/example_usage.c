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

/**
 * @file example_usage.c
 * @brief YAML 解析器使用示例
 *
 * 演示如何使用增强版 YAML 解析器读取配置文件
 */

#include <stdio.h>
#include "yaml_parse.h"

int main() {
    YamlDocument doc;
    yaml_doc_init(&doc);

    // 解析配置文件
    if (yaml_parse_file(&doc, "config.yaml") != 0) {
        printf("解析配置文件失败\n");
        return 1;
    }

    // ===== 示例 1: 读取简单配置项 =====
    printf("=== 应用配置 ===\n");
    printf("应用名称: %s\n", yaml_get_string(&doc, "app_name", "Unknown"));
    printf("版本号: %s\n", yaml_get_string(&doc, "version", "0.0.0"));
    printf("调试模式: %s\n", yaml_get_bool(&doc, "debug", 0) ? "开启" : "关闭");
    printf("最大连接数: %lld\n", (long long)yaml_get_int(&doc, "max_connections", 50));

    // ===== 示例 2: 读取嵌套配置 =====
    printf("\n=== 服务器配置 ===\n");
    printf("服务器名称: %s\n", yaml_get_string(&doc, "server.name", "Unknown"));
    printf("监听地址: %s\n", yaml_get_string(&doc, "server.host", "0.0.0.0"));
    printf("监听端口: %lld\n", (long long)yaml_get_int(&doc, "server.port", 8080));
    printf("超时时间: %.1f 秒\n", yaml_get_float(&doc, "server.timeout", 30.0));

    printf("\n=== SSL 配置 ===\n");
    printf("SSL 启用: %s\n", yaml_get_bool(&doc, "server.ssl.enabled", 0) ? "是" : "否");
    printf("证书路径: %s\n", yaml_get_string(&doc, "server.ssl.cert_path", ""));
    printf("密钥路径: %s\n", yaml_get_string(&doc, "server.ssl.key_path", ""));

    // ===== 示例 3: 读取数据库配置 =====
    printf("\n=== 数据库配置 ===\n");
    printf("驱动: %s\n", yaml_get_string(&doc, "database.connection.driver", "mysql"));
    printf("主机: %s\n", yaml_get_string(&doc, "database.connection.host", "localhost"));
    printf("端口: %lld\n", (long long)yaml_get_int(&doc, "database.connection.port", 3306));
    printf("用户名: %s\n", yaml_get_string(&doc, "database.connection.username", "root"));
    printf("连接池最大: %lld\n", (long long)yaml_get_int(&doc, "database.pool.max_size", 10));
    printf("连接池最小: %lld\n", (long long)yaml_get_int(&doc, "database.pool.min_size", 5));

    // ===== 示例 4: 读取数组/列表 =====
    printf("\n=== 服务器列表 ===\n");
    int server_count = yaml_get_count(&doc, "servers");
    for (int i = 0; i < server_count; i++) {
        YamlNode *server = yaml_get_list_item(&doc, "servers", i);
        if (server && server->type == YAML_TYPE_STRING) {
            printf("  [%d] %s\n", i, server->value.string_val);
        }
    }

    printf("\n=== 端口列表 ===\n");
    int port_count = yaml_get_count(&doc, "ports");
    for (int i = 0; i < port_count; i++) {
        YamlNode *port = yaml_get_list_item(&doc, "ports", i);
        if (port && port->type == YAML_TYPE_INT) {
            printf("  [%d] %lld\n", i, (long long)port->value.int_val);
        }
    }

    printf("\n=== 标签列表 ===\n");
    int tag_count = yaml_get_count(&doc, "tags");
    for (int i = 0; i < tag_count; i++) {
        YamlNode *tag = yaml_get_list_item(&doc, "tags", i);
        if (tag && tag->type == YAML_TYPE_STRING) {
            printf("  [%d] %s\n", i, tag->value.string_val);
        }
    }

    // ===== 示例 5: 读取复杂嵌套结构 =====
    printf("\n=== 日志配置 ===\n");
    printf("日志级别: %s\n", yaml_get_string(&doc, "logging.level", "INFO"));
    printf("控制台输出: %s\n", yaml_get_bool(&doc, "logging.handlers.console.enabled", 0) ? "启用" : "禁用");
    printf("文件输出: %s\n", yaml_get_bool(&doc, "logging.handlers.file.enabled", 0) ? "启用" : "禁用");
    printf("日志文件: %s\n", yaml_get_string(&doc, "logging.handlers.file.path", ""));
    printf("日志轮转: %s\n", yaml_get_bool(&doc, "logging.handlers.file.rotation.enabled", 0) ? "启用" : "禁用");
    printf("最大文件大小: %s\n", yaml_get_string(&doc, "logging.handlers.file.rotation.max_size", "100M"));
    printf("保留天数: %s\n", yaml_get_string(&doc, "logging.handlers.file.rotation.max_age", "30d"));

    // ===== 示例 6: 特性配置（深层嵌套） =====
    printf("\n=== 特性配置 ===\n");
    int feature_count = yaml_get_count(&doc, "features");
    for (int i = 0; i < feature_count; i++) {
        YamlNode *feature = yaml_get_list_item(&doc, "features", i);
        if (feature) {
            printf("特性 %d:\n", i + 1);
            printf("  名称: %s\n", feature->value.string_val);

            // 获取下一个节点（enabled）
            YamlNode *enabled_node = yaml_get_list_item(&doc, "features", i + 1);
            if (enabled_node && enabled_node->type == YAML_TYPE_BOOL) {
                printf("  启用: %s\n", enabled_node->value.bool_val ? "是" : "否");
            }
        }
    }

    // 清理资源
    yaml_doc_cleanup(&doc);

    return 0;
}
