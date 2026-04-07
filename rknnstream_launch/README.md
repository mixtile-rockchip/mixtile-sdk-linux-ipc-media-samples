# RKNNStream Demo Applications

RKNNStream演示应用程序，展示如何使用RKNNStream框架进行AI推理和多媒体处理。

## 目录结构

```
samples/rknnstream_demo/
├── rkns_demo.c              # 主demo程序源码
├── rkns_demo_config.c       # 配置文件解析实现
├── rkns_demo_config.h       # 配置文件解析头文件
├── rkns_demo_config.yaml    # YAML配置文件示例
├── Makefile                 # 编译脚本
├── README.md                # 本文档
└── yaml/                    # YAML解析库
    ├── yaml_parse.h         # YAML解析器头文件
    ├── yaml_parse.c         # YAML解析器实现
    ├── yaml_test.c          # YAML解析器测试
    ├── example_usage.c      # YAML使用示例
    ├── config.yaml          # 测试配置文件
    ├── README.md            # YAML库文档
    └── QUICKREF.md          # YAML库快速参考
```

## 功能特性

### YAML配置支持
- 基于自定义YAML解析器的配置管理
- 支持嵌套配置项和数组
- 自动类型识别（字符串、整数、浮点数、布尔值）
- 路径访问配置项（如：`server.host`）

### RKNNStream集成
- 视频输入/输出通道演示
- AI推理功能展示
- OSD overlay渲染示例
- 内存管理演示

## 编译

### 前置条件

1. **RKNNStream库已编译**
   ```bash
   cd /path/to/rknnstream
   ./skills/build-compile-skill-unified/build-unified.sh
   ```

2. **环境变量设置**（可选）
   ```bash
   export RKNS_PATH=/path/to/rknnstream
   export RKNS_BUILD=/path/to/rknnstream/build
   export RKNS_INSTALL=/path/to/install/dir
   ```

### 本地编译（x86_64）

```bash
make all
```

### 交叉编译

**ARM 32-bit:**
```bash
make cross-arm32
```

**ARM 64-bit:**
```bash
make cross-arm64
```

## 运行

### 本地运行

```bash
make run
```

或者直接运行编译好的程序：
```bash
./rkns_demo
```

### 部署到设备

1. **确保ADB已连接设备**
   ```bash
   adb devices
   ```

2. **部署程序和库**
   ```bash
   make deploy
   ```

3. **在设备上运行**
   ```bash
   adb shell
   cd /tmp
   /oem/usr/bin/rkns_demo_arm64
   ```

## 配置文件

### YAML配置示例

`rkns_demo_config.yaml`:
```yaml
# RKNNStream Demo配置文件

app:
  name: "rknnstream_demo"
  version: "1.0.0"
  log_level: "info"

input:
  source_type: "camera"    # camera, video, image
  device_id: 0
  width: 1920
  height: 1080
  fps: 30

output:
  display: true
  save_video: false
  output_path: "/tmp/output.mp4"

infer:
  model_path: "/oem/usr/share/models/yolov5.rknn"
  conf_threshold: 0.5
  nms_threshold: 0.4

overlay:
  enable: true
  show_fps: true
  show_confidence: true
  box_color:
    r: 255
    g: 0
    b: 0
```

### 代码中使用配置

```c
#include "rkns_demo_config.h"

int main() {
    // 加载配置文件
    DemoConfig config;
    if (load_config("rkns_demo_config.yaml", &config) != 0) {
        printf("Failed to load config\n");
        return -1;
    }

    // 使用配置
    printf("App name: %s\n", config.app.name);
    printf("Input width: %d\n", config.input.width);

    // 释放配置
    free_config(&config);
    return 0;
}
```

## YAML解析器

详细的YAML解析器文档请参考：
- `yaml/README.md` - 完整文档
- `yaml/QUICKREF.md` - 快速参考
- `yaml/example_usage.c` - 使用示例

## 常见问题

### 编译错误

**找不到头文件**
- 检查`RKNS_PATH`环境变量是否正确
- 确保RKNNStream库已编译

**链接错误**
- 检查`RKNS_BUILD`和`RKNS_INSTALL`路径
- 确保librkns.so已生成

### 运行错误

**找不到库文件**
```bash
export LD_LIBRARY_PATH=/oem/usr/lib:$LD_LIBRARY_PATH
```

**配置文件加载失败**
- 检查配置文件路径是否正确
- 验证YAML语法是否正确

## 更多信息

- RKNNStream项目文档: `/path/to/rknnstream/CLAUDE.md`
- API参考: `/path/to/rknnstream/include/`
- 示例代码: `/path/to/rknnstream/samples/`

## 许可证

Copyright (c) 2026 by Rockchip Electronics Co., Ltd. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
