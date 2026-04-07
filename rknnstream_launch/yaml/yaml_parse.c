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
#include <ctype.h>
#include "yaml_parse.h"

#define MAX_LINE_LEN  2048
#define INDENT_SPACES 2  // 每层缩进的空格数

/**
 * @brief 去除字符串首尾空白字符
 */
static void trim_whitespace(char *str) {
    if (str == NULL || *str == '\0') return;

    // 去除尾部空白
    char *end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }

    // 去除头部空白
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    // 内存移动
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * @brief 计算行首的缩进级别
 */
static int calculate_indent(const char *line) {
    int spaces = 0;
    while (*line && *line == ' ') {
        spaces++;
        line++;
    }
    return spaces / INDENT_SPACES;  // 每层缩进 INDENT_SPACES 个空格
}

/**
 * @brief 检测字符串的类型并转换
 */
static YamlType detect_type(const char *str, void *out_val) {
    if (str == NULL || strlen(str) == 0) {
        return YAML_TYPE_NONE;
    }

    // 去除首尾引号（YAML 引号字符串："..." 或 '...'）
    char buf[MAX_VAL_LEN];
    const char *val = str;
    size_t len = strlen(str);
    if (len >= 2 &&
        ((str[0] == '"' && str[len - 1] == '"') ||
         (str[0] == '\'' && str[len - 1] == '\''))) {
        strncpy(buf, str + 1, len - 2);
        buf[len - 2] = '\0';
        val = buf;
    }

    // 检查布尔值
    if (strcmp(val, "true") == 0 || strcmp(val, "True") == 0 ||
        strcmp(val, "yes") == 0 || strcmp(val, "on") == 0) {
        *(int*)out_val = 1;
        return YAML_TYPE_BOOL;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "False") == 0 ||
        strcmp(val, "no") == 0 || strcmp(val, "off") == 0) {
        *(int*)out_val = 0;
        return YAML_TYPE_BOOL;
    }

    // 检查整数
    char *end;
    long long int_val = strtoll(val, &end, 10);
    if (*end == '\0') {
        *(int64_t*)out_val = int_val;
        return YAML_TYPE_INT;
    }

    // 检查浮点数
    double float_val = strtod(val, &end);
    if (*end == '\0') {
        *(double*)out_val = float_val;
        return YAML_TYPE_FLOAT;
    }

    // 默认为字符串
    strncpy((char*)out_val, val, MAX_VAL_LEN - 1);
    return YAML_TYPE_STRING;
}

/**
 * @brief 初始化 YAML 文档
 */
void yaml_doc_init(YamlDocument* doc) {
    memset(doc, 0, sizeof(YamlDocument));
    doc->capacity = INITIAL_CAPACITY;
    doc->nodes = (YamlNode*)calloc(doc->capacity, sizeof(YamlNode));
    doc->root_idx = -1;
}

/**
 * @brief 清理 YAML 文档
 */
void yaml_doc_cleanup(YamlDocument* doc) {
    if (doc->nodes) {
        free(doc->nodes);
        doc->nodes = NULL;
    }
    doc->count = 0;
    doc->capacity = 0;
}

/**
 * @brief 添加新节点
 */
static YamlNode* add_node(YamlDocument* doc) {
    if (doc->count >= doc->capacity) {
        doc->capacity *= 2;
        doc->nodes = (YamlNode*)realloc(doc->nodes, doc->capacity * sizeof(YamlNode));
    }

    YamlNode* node = &doc->nodes[doc->count];
    memset(node, 0, sizeof(YamlNode));
    node->index = doc->count;
    doc->count++;

    return node;
}

/**
 * @brief 解析单行 YAML
 * @return 0 成功，-1 跳过此行，-2 错误
 */
static int parse_line(YamlDocument* doc, const char *line_str, int line_num) {
    (void)line_num;
    char line[MAX_LINE_LEN];
    strncpy(line, line_str, MAX_LINE_LEN - 1);
    line[MAX_LINE_LEN - 1] = '\0';

    // 去除尾部空白和换行符
    trim_whitespace(line);

    // 跳过空行和注释
    if (strlen(line) == 0 || line[0] == '#') {
        return -1;  // 跳过
    }

    // 计算缩进级别
    int depth = calculate_indent(line_str);

    // 检查是否为列表项（- 开头）
    int is_list = 0;
    char *content = line;
    while (*content && isspace((unsigned char)*content)) {
        content++;
    }
    if (*content == '-') {
        is_list = 1;
        content++;  // 跳过 '-'
        while (*content && isspace((unsigned char)*content)) {
            content++;
        }
    }

    // 移除行内注释
    char *comment = strchr(content, '#');
    if (comment) {
        *comment = '\0';
    }
    trim_whitespace(content);

    // 查找键值分隔符 ':'
    char *colon = strchr(content, ':');
    YamlNode *node = add_node(doc);
    node->depth = depth;

    if (is_list) {
        if (colon && colon > content) {
            // 列表项包含键值对（如 "- name: value"）
            // 创建容器节点（OBJECT）代表列表项，再创建子节点存放键值对
            node->type = YAML_TYPE_OBJECT;
            node->key[0] = '\0';  // 列表项容器没有键名

            *colon = '\0';
            char *key = content;
            char *value = colon + 1;
            trim_whitespace(key);
            trim_whitespace(value);

            // 创建子节点存放第一个键值对
            YamlNode *child = add_node(doc);
            child->depth = depth + 1;
            strncpy(child->key, key, MAX_KEY_LEN - 1);
            void *val_ptr = child->value.string_val;
            child->type = detect_type(value, val_ptr);
        } else if (*content) {
            // 列表项只有值（如 "- item1"）
            node->type = YAML_TYPE_LIST;
            node->key[0] = '\0';
            void *val_ptr = node->value.string_val;
            node->type = detect_type(content, val_ptr);
        } else {
            // 空列表项
            node->type = YAML_TYPE_LIST;
            node->key[0] = '\0';
        }
    } else if (colon) {
        // 键值对
        *colon = '\0';
        char *key = content;
        char *value = colon + 1;
        trim_whitespace(key);
        trim_whitespace(value);

        strncpy(node->key, key, MAX_KEY_LEN - 1);

        if (*value == '\0') {
            // 只有键，没有值（对象节点）
            node->type = YAML_TYPE_OBJECT;
        } else {
            // 有值，检测类型
            void *val_ptr = node->value.string_val;
            node->type = detect_type(value, val_ptr);
        }
    } else {
        // 只有值（可能是列表的续行？暂不支持）
        node->type = YAML_TYPE_STRING;
        strncpy(node->value.string_val, content, MAX_VAL_LEN - 1);
    }

    return 0;
}

/**
 * @brief 构建树形结构（建立父子关系）
 */
static void build_tree(YamlDocument* doc) {
    if (doc->count == 0) return;

    doc->root_idx = 0;
    doc->nodes[0].parent_idx = -1;

    for (int i = 1; i < doc->count; i++) {
        YamlNode *node = &doc->nodes[i];

        // 查找父节点（深度比当前小 1 的最近节点）
        node->parent_idx = -1;
        for (int j = i - 1; j >= 0; j--) {
            if (doc->nodes[j].depth == node->depth - 1) {
                node->parent_idx = j;
                break;
            }
        }

        // 如果没有找到父节点且深度 > 0，则将其作为根节点
        if (node->parent_idx == -1 && node->depth > 0) {
            node->parent_idx = 0;  // 默认挂在根节点下
        }
    }

    // 统计子节点数量
    for (int i = 0; i < doc->count; i++) {
        YamlNode *node = &doc->nodes[i];
        if (node->type == YAML_TYPE_OBJECT || node->type == YAML_TYPE_LIST) {
            node->value.container.count = 0;
            node->value.container.first_child_idx = -1;

            // 查找直接子节点
            for (int j = i + 1; j < doc->count; j++) {
                if (doc->nodes[j].parent_idx == i) {
                    if (node->value.container.first_child_idx == -1) {
                        node->value.container.first_child_idx = j;
                    }
                    node->value.container.count++;
                } else if (doc->nodes[j].depth <= node->depth) {
                    break;  // 遇到同级或更高级的节点，停止查找
                }
            }
        }
    }
}

/**
 * @brief 从文件解析 YAML
 */
int yaml_parse_file(YamlDocument* doc, const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("打开文件失败");
        return -1;
    }

    char line[MAX_LINE_LEN];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        parse_line(doc, line, line_num);
    }

    fclose(fp);

    // 构建树形结构
    build_tree(doc);

    return 0;
}

/**
 * @brief 根据路径查找节点（支持点号分隔的路径）
 */
YamlNode* yaml_get_node(YamlDocument* doc, const char* path) {
    if (doc == NULL || path == NULL || doc->count == 0) {
        return NULL;
    }

    // 复制路径以便分割
    char path_copy[MAX_VAL_LEN];
    strncpy(path_copy, path, MAX_VAL_LEN - 1);
    path_copy[MAX_VAL_LEN - 1] = '\0';

    // 分割路径
    char *token = strtok(path_copy, ".");
    YamlNode *current_node = NULL;

    while (token != NULL) {
        YamlNode *found = NULL;

        // 检查 token 是否为数字索引（如 "0", "1", "2"）
        int is_numeric_index = 0;
        int index_val = 0;
        if (token[0] != '\0') {
            is_numeric_index = 1;
            for (int k = 0; token[k] != '\0'; k++) {
                if (token[k] < '0' || token[k] > '9') {
                    is_numeric_index = 0;
                    break;
                }
                index_val = index_val * 10 + (token[k] - '0');
            }
        }

        // 如果是第一个 token，从根节点开始查找
        if (current_node == NULL) {
            for (int i = 0; i < doc->count; i++) {
                if (doc->nodes[i].depth == 0 &&
                    strcmp(doc->nodes[i].key, token) == 0) {
                    found = &doc->nodes[i];
                    break;
                }
            }
        } else if (is_numeric_index) {
            // 数字 token → 按索引访问子节点
            int child_idx = current_node->value.container.first_child_idx;
            int count = 0;
            for (int i = child_idx; i >= 0 && i < doc->count; i++) {
                YamlNode *child = &doc->nodes[i];
                if (child->parent_idx == current_node->index) {
                    if (count == index_val) {
                        found = child;
                        break;
                    }
                    count++;
                } else if (child->depth <= current_node->depth) {
                    break;
                }
            }
        } else {
            // 从当前节点的子节点中按键名查找
            int first_child = current_node->value.container.first_child_idx;
            if (first_child >= 0) {
                for (int i = first_child; i < doc->count; i++) {
                    YamlNode *child = &doc->nodes[i];
                    if (child->parent_idx != current_node->index) {
                        // 深度回到当前节点层级或更浅，所有子节点已遍历完毕
                        if (child->depth <= current_node->depth) {
                            break;
                        }
                        continue;  // 跳过孙子节点
                    }
                    if (strcmp(child->key, token) == 0) {
                        found = child;
                        break;
                    }
                }
            }
        }

        if (found == NULL) {
            return NULL;  // 未找到
        }

        current_node = found;
        token = strtok(NULL, ".");
    }

    return current_node;
}

/**
 * @brief 获取字符串值
 */
const char* yaml_get_string(YamlDocument* doc, const char* path, const char* default_val) {
    YamlNode *node = yaml_get_node(doc, path);
    if (node == NULL || node->type != YAML_TYPE_STRING) {
        return default_val;
    }
    return node->value.string_val;
}

/**
 * @brief 获取整数值
 */
int64_t yaml_get_int(YamlDocument* doc, const char* path, int64_t default_val) {
    YamlNode *node = yaml_get_node(doc, path);
    if (node == NULL) {
        return default_val;
    }
    switch (node->type) {
        case YAML_TYPE_INT:
            return node->value.int_val;
        case YAML_TYPE_STRING:
            return atoll(node->value.string_val);
        case YAML_TYPE_BOOL:
            return node->value.bool_val;
        default:
            return default_val;
    }
}

/**
 * @brief 获取布尔值
 */
int yaml_get_bool(YamlDocument* doc, const char* path, int default_val) {
    YamlNode *node = yaml_get_node(doc, path);
    if (node == NULL) {
        return default_val;
    }
    switch (node->type) {
        case YAML_TYPE_BOOL:
            return node->value.bool_val;
        case YAML_TYPE_INT:
            return node->value.int_val != 0;
        default:
            return default_val;
    }
}

/**
 * @brief 获取浮点数值
 */
double yaml_get_float(YamlDocument* doc, const char* path, double default_val) {
    YamlNode *node = yaml_get_node(doc, path);
    if (node == NULL) {
        return default_val;
    }
    switch (node->type) {
        case YAML_TYPE_FLOAT:
            return node->value.float_val;
        case YAML_TYPE_INT:
            return (double)node->value.int_val;
        case YAML_TYPE_STRING:
            return atof(node->value.string_val);
        default:
            return default_val;
    }
}

/**
 * @brief 获取子节点数量
 */
int yaml_get_count(YamlDocument* doc, const char* path) {
    YamlNode *node = yaml_get_node(doc, path);
    if (node == NULL) {
        return -1;
    }
    if (node->type == YAML_TYPE_OBJECT || node->type == YAML_TYPE_LIST) {
        return node->value.container.count;
    }
    return 0;
}

/**
 * @brief 获取列表指定索引的子节点
 */
YamlNode* yaml_get_list_item(YamlDocument* doc, const char* path, int index) {
    YamlNode *parent = yaml_get_node(doc, path);
    if (parent == NULL || index < 0) {
        return NULL;
    }

    if (parent->type != YAML_TYPE_LIST && parent->type != YAML_TYPE_OBJECT) {
        return NULL;
    }

    int child_idx = parent->value.container.first_child_idx;
    int count = 0;

    while (child_idx < doc->count && count <= index) {
        YamlNode *child = &doc->nodes[child_idx];
        if (child->parent_idx == parent->index) {
            if (count == index) {
                return child;
            }
            count++;
        }
        child_idx++;
    }

    return NULL;
}

/**
 * @brief 获取类型字符串
 */
const char* yaml_type_to_string(YamlType type) {
    switch (type) {
        case YAML_TYPE_NONE:    return "NONE";
        case YAML_TYPE_STRING:  return "STRING";
        case YAML_TYPE_INT:     return "INT";
        case YAML_TYPE_BOOL:    return "BOOL";
        case YAML_TYPE_FLOAT:   return "FLOAT";
        case YAML_TYPE_LIST:    return "LIST";
        case YAML_TYPE_OBJECT:  return "OBJECT";
        default:                return "UNKNOWN";
    }
}

/**
 * @brief 打印节点（递归）
 */
static void print_node(YamlDocument* doc, YamlNode *node, int level) {
    (void)doc;
    if (node == NULL) return;

    // 打印缩进和键
    for (int i = 0; i < level; i++) {
        printf("  ");
    }

    if (node->key[0] != '\0') {
        printf("%s: ", node->key);
    } else {
        printf("- ");
    }

    // 打印值或类型信息
    switch (node->type) {
        case YAML_TYPE_STRING:
            printf("%s [STRING]\n", node->value.string_val);
            break;
        case YAML_TYPE_INT:
            printf("%lld [INT]\n", (long long)node->value.int_val);
            break;
        case YAML_TYPE_BOOL:
            printf("%s [BOOL]\n", node->value.bool_val ? "true" : "false");
            break;
        case YAML_TYPE_FLOAT:
            printf("%f [FLOAT]\n", node->value.float_val);
            break;
        case YAML_TYPE_LIST:
            printf("[LIST] (%d items)\n", node->value.container.count);
            break;
        case YAML_TYPE_OBJECT:
            printf("[OBJECT] (%d children)\n", node->value.container.count);
            break;
        default:
            printf("[UNKNOWN]\n");
            break;
    }
}

/**
 * @brief 打印 YAML 文档树
 */
void yaml_print_tree(YamlDocument* doc) {
    printf("===== YAML 文档树结构 =====\n");
    printf("总节点数: %d\n\n", doc->count);

    for (int i = 0; i < doc->count; i++) {
        print_node(doc, &doc->nodes[i], doc->nodes[i].depth);
    }

    printf("\n===== 树形可视化结束 =====\n");
}
