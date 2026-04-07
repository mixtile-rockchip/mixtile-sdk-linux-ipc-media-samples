# 简易 YAML 解析器

纯 C 语言实现的轻量级 YAML 解析器，支持嵌套对象和数组列表，无需第三方依赖。

## 特性

✅ **纯 C 实现** - 无任何第三方库依赖
✅ **嵌套对象** - 支持多层嵌套配置（无限层级）
✅ **数组列表** - 支持列表和数组
✅ **类型自动识别** - 自动识别字符串、整数、布尔值、浮点数
✅ **树形结构** - 内部使用树形数据结构存储
✅ **路径访问** - 支持点号分隔的路径访问嵌套值
✅ **默认值** - 所有获取函数都支持默认值
✅ **交叉编译** - 支持 ARM 交叉编译

## 编译

### 本地编译（x86_64）

```bash
gcc -o yaml_test yaml_test.c yaml_parse.c -Wall
./yaml_test config.yaml
```

### 交叉编译（ARM 32-bit）

```bash
arm-rockchip1240-linux-gnueabihf-gcc -o yaml_test yaml_test.c yaml_parse.c -Wall
```

### 交叉编译（ARM 64-bit）

```bash
aarch64-rockchip1240-linux-gnu-gcc -o yaml_test yaml_test.c yaml_parse.c -Wall
```

## API 文档

### 初始化与清理

```c
// 初始化 YAML 文档
void yaml_doc_init(YamlDocument* doc);

// 清理 YAML 文档（释放内存）
void yaml_doc_cleanup(YamlDocument* doc);
```

### 解析文件

```c
// 从文件解析 YAML
int yaml_parse_file(YamlDocument* doc, const char* filename);
```

### 获取值（支持路径访问）

```c
// 获取字符串值
const char* yaml_get_string(YamlDocument* doc, const char* path, const char* default_val);

// 获取整数值
int64_t yaml_get_int(YamlDocument* doc, const char* path, int64_t default_val);

// 获取布尔值
int yaml_get_bool(YamlDocument* doc, const char* path, int default_val);

// 获取浮点数值
double yaml_get_float(YamlDocument* doc, const char* path, double default_val);
```

### 数组/列表操作

```c
// 获取列表/对象的子节点数量
int yaml_get_count(YamlDocument* doc, const char* path);

// 获取列表指定索引的节点
YamlNode* yaml_get_list_item(YamlDocument* doc, const char* path, int index);
```

### 工具函数

```c
// 打印 YAML 文档树形结构
void yaml_print_tree(YamlDocument* doc);

// 获取节点类型字符串（用于调试）
const char* yaml_type_to_string(YamlType type);
```

## 使用示例

### YAML 配置文件示例

```yaml
# 简单键值对
app_name: 我的应用
version: 1.0.0
debug: true
max_connections: 100

# 嵌套对象
server:
  name: MyServer
  host: 0.0.0.0
  port: 8080
  ssl:
    enabled: true
    cert_path: /etc/ssl/cert.pem

# 数组列表
servers:
  - server1.example.com
  - server2.example.com
  - server3.example.com

# 对象数组
users:
  - name: Alice
    age: 30
    admin: true
  - name: Bob
    age: 25
    admin: false
```

### C 代码示例

```c
#include <stdio.h>
#include "yaml_parse.h"

int main() {
    YamlDocument doc;
    yaml_doc_init(&doc);

    // 解析配置文件
    if (yaml_parse_file(&doc, "config.yaml") != 0) {
        printf("解析失败\n");
        return 1;
    }

    // 读取简单配置
    printf("应用名称: %s\n", yaml_get_string(&doc, "app_name", "Unknown"));
    printf("版本号: %s\n", yaml_get_string(&doc, "version", "0.0.0"));
    printf("调试模式: %s\n", yaml_get_bool(&doc, "debug", 0) ? "开启" : "关闭");
    printf("最大连接: %lld\n", (long long)yaml_get_int(&doc, "max_connections", 50));

    // 读取嵌套配置（使用点号分隔路径）
    printf("服务器名称: %s\n", yaml_get_string(&doc, "server.name", "Unknown"));
    printf("服务器端口: %lld\n", (long long)yaml_get_int(&doc, "server.port", 8080));
    printf("SSL启用: %s\n", yaml_get_bool(&doc, "server.ssl.enabled", 0) ? "是" : "否");

    // 遍历数组
    int server_count = yaml_get_count(&doc, "servers");
    printf("服务器列表 (%d 个):\n", server_count);
    for (int i = 0; i < server_count; i++) {
        YamlNode *server = yaml_get_list_item(&doc, "servers", i);
        if (server && server->type == YAML_TYPE_STRING) {
            printf("  [%d] %s\n", i, server->value.string_val);
        }
    }

    // 清理资源
    yaml_doc_cleanup(&doc);
    return 0;
}
```

## 支持的数据类型

| 类型 | YAML 示例 | C 类型 | 获取函数 |
|------|-----------|--------|----------|
| 字符串 | `name: "value"` 或 `name: value` | `char*` | `yaml_get_string()` |
| 整数 | `port: 8080` | `int64_t` | `yaml_get_int()` |
| 布尔值 | `enabled: true` / `enabled: false` | `int` | `yaml_get_bool()` |
| 浮点数 | `timeout: 30.5` | `double` | `yaml_get_float()` |
| 列表 | `- item1` | - | `yaml_get_count()` + `yaml_get_list_item()` |
| 对象 | `key: <nested>` | - | 路径访问 |

## 路径访问语法

使用点号（`.`）分隔嵌套层级：

```c
// 一级嵌套
yaml_get_string(&doc, "server.name", "default")

// 二级嵌套
yaml_get_string(&doc, "database.connection.host", "localhost")

// 三级嵌套
yaml_get_bool(&doc, "logging.handlers.file.rotation.enabled", 0)
```

## 数组/列表语法

### 简单列表

```yaml
servers:
  - server1.example.com
  - server2.example.com
  - server3.example.com
```

访问方式：

```c
int count = yaml_get_count(&doc, "servers");
for (int i = 0; i < count; i++) {
    YamlNode *item = yaml_get_list_item(&doc, "servers", i);
    // 使用 item->value.string_val
}
```

### 对象列表

```yaml
users:
  - name: Alice
    age: 30
  - name: Bob
    age: 25
```

访问方式：

```c
int count = yaml_get_count(&doc, "users");
for (int i = 0; i < count; i++) {
    YamlNode *item = yaml_get_list_item(&doc, "users", i);
    // 遍历列表项的子节点
}
```

## 限制

1. **缩进规范**：每层缩进必须为 2 个空格（可通过 `INDENT_SPACES` 宏修改）
2. **列表语法**：仅支持 `-` 开头的列表项
3. **多行字符串**：不支持多行字符串（`>` 或 `|` 语法）
4. **引用**：不支持 YAML 引用和锚点（`&` 和 `*`）
5. **复杂类型**：不支持日期、时间等特殊类型

## 测试

运行测试套件：

```bash
./yaml_test config.yaml
```

测试内容：
- ✅ 嵌套对象解析
- ✅ 数组/列表解析
- ✅ 类型自动识别
- ✅ 默认值处理
- ✅ 路径访问
- ✅ 类型转换

## 项目文件

```
yaml/
├── yaml_parse.h      # 头文件（API 声明）
├── yaml_parse.c      # 实现文件
├── yaml_test.c       # 测试程序
├── example_usage.c   # 使用示例
├── config.yaml       # 测试配置文件
└── README.md         # 本文档
```

## 集成到项目

### 方法 1：直接编译

```bash
gcc your_app.c yaml_parse.c -o your_app
```

### 方法 2：作为库使用

```bash
# 编译为静态库
ar rcs libyamlparse.a yaml_parse.o

# 链接到你的项目
gcc your_app.c -L. -lyamlparse -o your_app
```

### 方法 3：添加到 Makefile/CMake

```makefile
# Makefile
YAML_SRCS = third-party/yaml/yaml_parse.c
YAML_INCLUDES = -Ithird-party/yaml

your_app: your_app.c
    gcc $(YAML_INCLUDES) your_app.c $(YAML_SRCS) -o your_app
```

## 技术细节

### 数据结构

```c
// 节点类型
typedef enum {
    YAML_TYPE_NONE,     // 未初始化
    YAML_TYPE_STRING,   // 字符串
    YAML_TYPE_INT,      // 整数
    YAML_TYPE_BOOL,     // 布尔
    YAML_TYPE_FLOAT,    // 浮点数
    YAML_TYPE_LIST,     // 列表
    YAML_TYPE_OBJECT    // 对象
} YamlType;

// 节点结构
typedef struct YamlNode {
    char key[128];           // 键名
    YamlType type;           // 类型
    int depth;               // 嵌套深度
    int index;               // 节点索引
    int parent_idx;          // 父节点索引
    union {
        char string_val[512];
        int64_t int_val;
        int bool_val;
        double float_val;
        struct {
            int count;
            int first_child_idx;
        } container;
    } value;
} YamlNode;

// 文档结构
typedef struct {
    YamlNode* nodes;
    int count;
    int capacity;
    int root_idx;
} YamlDocument;
```

### 算法特点

1. **单次解析**：一次遍历完成解析
2. **树形构建**：基于缩进构建树形结构
3. **路径查找**：深度优先搜索节点
4. **类型推断**：启发式类型识别

## 许可证

本项目为 Rockchip RKNNStream 项目的子模块，遵循项目整体许可证。

## 作者

Rockchip 多媒体框架团队

## 更新日志

### v2.0 (当前版本)
- ✅ 新增嵌套对象支持
- ✅ 新增数组/列表支持
- ✅ 新增路径访问功能
- ✅ 新增类型自动识别
- ✅ 重构为树形数据结构

### v1.0 (初始版本)
- ✅ 基础键值对解析
- ✅ 简单类型支持（字符串、整数、布尔）
