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

#ifndef YAML_PARSE_H
#define YAML_PARSE_H

#include <stdint.h>

#define MAX_KEY_LEN      128     // 键最大长度
#define MAX_VAL_LEN      512     // 值最大长度
#define MAX_DEPTH        16      // 最大嵌套深度
#define INITIAL_CAPACITY 256     // 初始容量

// YAML 值类型
typedef enum {
    YAML_TYPE_NONE = 0,          // 未初始化
    YAML_TYPE_STRING,            // 字符串值
    YAML_TYPE_INT,               // 整数值
    YAML_TYPE_BOOL,              // 布尔值
    YAML_TYPE_FLOAT,             // 浮点数值
    YAML_TYPE_LIST,              // 列表（数组）
    YAML_TYPE_OBJECT             // 对象（嵌套）
} YamlType;

// 前向声明
typedef struct YamlNode YamlNode;

// YAML 节点结构（支持树形层级）
struct YamlNode {
    char key[MAX_KEY_LEN];       // 键名（列表元素为空）
    YamlType type;               // 值类型
    int depth;                   // 嵌套深度（0为根级）
    int index;                   // 在父节点中的索引
    int parent_idx;              // 父节点索引（-1表示根节点）

    // 值数据（联合体）
    union {
        char string_val[MAX_VAL_LEN];  // 字符串值
        int64_t int_val;               // 整数值
        int bool_val;                  // 布尔值（0/1）
        double float_val;              // 浮点数值
        struct {
            int count;                 // 子节点数量
            int first_child_idx;       // 第一个子节点索引
        } container;                   // 对象或列表
    } value;
};

// YAML 文档结构
typedef struct {
    YamlNode* nodes;              // 节点数组
    int count;                    // 当前节点数
    int capacity;                 // 节点数组容量
    int root_idx;                 // 根节点索引
} YamlDocument;

/**
 * @brief 初始化 YAML 文档
 */
void yaml_doc_init(YamlDocument* doc);

/**
 * @brief 清理 YAML 文档（释放内存）
 */
void yaml_doc_cleanup(YamlDocument* doc);

/**
 * @brief 从文件解析 YAML
 * @param doc YAML 文档
 * @param filename 文件路径
 * @return 0 成功，负数 失败
 */
int yaml_parse_file(YamlDocument* doc, const char* filename);

/**
 * @brief 根据路径获取节点（支持嵌套访问）
 * @param doc YAML 文档
 * @param path 路径（如 "server.port" 或 "database.host"）
 * @return 节点指针，未找到返回 NULL
 */
YamlNode* yaml_get_node(YamlDocument* doc, const char* path);

/**
 * @brief 根据路径获取字符串值
 * @param doc YAML 文档
 * @param path 路径（如 "server.port"）
 * @param default_val 默认值（未找到时返回）
 * @return 字符串值
 */
const char* yaml_get_string(YamlDocument* doc, const char* path, const char* default_val);

/**
 * @brief 根据路径获取整数值
 */
int64_t yaml_get_int(YamlDocument* doc, const char* path, int64_t default_val);

/**
 * @brief 根据路径获取布尔值
 */
int yaml_get_bool(YamlDocument* doc, const char* path, int default_val);

/**
 * @brief 根据路径获取浮点数值
 */
double yaml_get_float(YamlDocument* doc, const char* path, double default_val);

/**
 * @brief 获取列表/对象的子节点数量
 * @param doc YAML 文档
 * @param path 父节点路径
 * @return 子节点数量，失败返回 -1
 */
int yaml_get_count(YamlDocument* doc, const char* path);

/**
 * @brief 获取列表指定索引的节点
 * @param doc YAML 文档
 * @param path 列表路径（如 "servers"）
 * @param index 索引（从 0 开始）
 * @return 子节点指针，失败返回 NULL
 */
YamlNode* yaml_get_list_item(YamlDocument* doc, const char* path, int index);

/**
 * @brief 打印 YAML 文档树形结构
 */
void yaml_print_tree(YamlDocument* doc);

/**
 * @brief 获取节点类型字符串（用于调试）
 */
const char* yaml_type_to_string(YamlType type);

#endif // YAML_PARSE_H
