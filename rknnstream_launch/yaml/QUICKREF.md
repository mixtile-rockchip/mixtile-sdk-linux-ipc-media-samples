# YAML 解析器 - 快速参考

## 快速开始

```bash
# 编译
make all

# 运行测试
make test

# 运行示例
make example

# 交叉编译（ARM）
make cross-arm32
```

## 常用 API

```c
#include "yaml_parse.h"

// 1. 初始化
YamlDocument doc;
yaml_doc_init(&doc);

// 2. 解析文件
yaml_parse_file(&doc, "config.yaml");

// 3. 获取值
const char *name = yaml_get_string(&doc, "app.name", "default");
int64_t port = yaml_get_int(&doc, "server.port", 8080);
int enabled = yaml_get_bool(&doc, "debug", 0);
double timeout = yaml_get_float(&doc, "timeout", 30.0);

// 4. 操作数组
int count = yaml_get_count(&doc, "servers");
for (int i = 0; i < count; i++) {
    YamlNode *item = yaml_get_list_item(&doc, "servers", i);
    // 使用 item->value.string_val
}

// 5. 清理
yaml_doc_cleanup(&doc);
```

## YAML 语法示例

```yaml
# 简单值
name: My App
port: 8080
enabled: true
timeout: 30.5

# 嵌套对象
server:
  host: 0.0.0.0
  port: 8080
  ssl:
    enabled: true

# 数组
servers:
  - server1.com
  - server2.com
  - server3.com

# 对象数组
users:
  - name: Alice
    age: 30
  - name: Bob
    age: 25
```

## 路径访问

```c
// 一级
yaml_get_string(&doc, "name", "default")

// 二级
yaml_get_int(&doc, "server.port", 8080)

// 三级
yaml_get_bool(&doc, "server.ssl.enabled", 0)
```

## 编译命令

```bash
# x86_64
gcc -o app your_app.c yaml_parse.c

# ARM 32-bit
arm-rockchip1240-linux-gnueabihf-gcc -o app your_app.c yaml_parse.c

# ARM 64-bit
aarch64-rockchip1240-linux-gnu-gcc -o app your_app.c yaml_parse.c
```

## 支持的类型

| YAML | C 类型 | 函数 |
|------|--------|------|
| `value` | `char*` | `yaml_get_string()` |
| `123` | `int64_t` | `yaml_get_int()` |
| `true/false` | `int` | `yaml_get_bool()` |
| `3.14` | `double` | `yaml_get_float()` |
| `- item` | - | `yaml_get_count()` |

## 项目文件

- `yaml_parse.h` - 头文件
- `yaml_parse.c` - 实现
- `yaml_test.c` - 测试程序
- `example_usage.c` - 使用示例
- `config.yaml` - 示例配置
- `Makefile` - 构建脚本
- `README.md` - 完整文档

## 测试结果

✅ 所有测试通过：
- 嵌套对象解析
- 数组/列表解析
- 类型自动识别
- 路径访问
- 默认值处理
- 类型转换

## 限制

- 缩进必须为 2 个空格
- 不支持多行字符串
- 不支持引用和锚点
- 仅支持 `-` 开头的列表

## 更多信息

详见 `README.md` 获取完整文档。
