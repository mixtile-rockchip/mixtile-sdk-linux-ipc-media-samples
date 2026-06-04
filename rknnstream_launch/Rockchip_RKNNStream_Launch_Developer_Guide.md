# RKNNStream Launch 开发者指南

RKNNStream Launch 是一个基于 RKNNStream 和 Rockit 框架的完整应用演示程序，支持通过 YAML 配置文件灵活地构建 AI 推理流水线。

## 目录结构

```
samples/rknnstream_launch/
├── Rockchip_RKNNStream_Launch_Developer_Guide.md  # 本文档
├── rkns_launch.c                                  # 主程序源码
├── rkns_launch_config.c                           # 配置管理源码
├── rkns_launch_config.h                           # 配置管理头文件
├── rkns_yolo_config.yaml                          # YOLO 目标检测配置示例
├── rkns_asr_config.yaml                           # 语音识别配置示例
├── rkns_asr_llm_config.yaml                       # 语音识别+LLM配置示例
├── Makefile                                       # 编译脚本
├── yaml/                                          # YAML 解析库
│   ├── yaml_parse.h                               # YAML 解析器头文件
│   ├── yaml_parse.c                               # YAML 解析器实现
│   ├── yaml_test.c                                # YAML 解析器测试
│   ├── example_usage.c                            # YAML 使用示例
│   ├── config.yaml                                # 测试配置文件
│   ├── launch.sh                                  # 测试脚本
│   ├── READMINE.md                                # YAML 库文档
│   ├── QUICKREF.md                                # YAML 库快速参考
│   └── Makefile                                   # YAML 库编译脚本
└── rkns-launch-1.0/                               # （可选）已编译的可执行文件目录
```

### 关键文件说明

| 文件 | 说明 |
|------|------|
| `rkns_launch.c` | 主程序入口，包含命令行解析、模块管理、流水线控制等核心逻辑 |
| `rkns_launch_config.h` | 配置结构体定义、模块操作函数表声明、类型解析函数声明 |
| `rkns_launch_config.c` | YAML 配置解析实现、模块操作函数表定义 |
| `rkns_yolo_config.yaml` | YOLO 目标检测场景的完整配置示例 |
| `rkns_asr_config.yaml` | 语音识别场景的完整配置示例 |
| `yaml/` | 轻量级 YAML 解析库，支持路径访问、类型识别等功能 |

## YAML 文件书写规则

### 整体结构

YAML 配置文件分为三个主要部分：
- `pipelines`：流水线配置数组（必需）
- `runtime`：运行时配置（可选）
- 其他：特定应用的配置（可选）

```yaml
# Pipeline 配置数组
pipelines:
  - name: "pipeline_name"
    type: "rknnstream"  # 或 "rockit"
    enabled: true
    modules: [...]
    bindings: [...]
    attaches: [...]

# 运行时配置
runtime:
  timeout: 0
  profiling: false
  verbose: true
```

### Pipeline 类型

支持两种流水线类型：

| 类型 | 说明 |
|------|------|
| `rknnstream` | RKNNStream 框架流水线，用于 AI 推理 |
| `rockit` | Rockit 多媒体框架流水线，用于视频/音频输入输出 |

### RKNNStream 模块类型

| 模块名 | 说明 |
|--------|------|
| `INPUT` | 输入模块，从 Rockit 或自定义源获取数据 |
| `INFER` | 推理模块，执行 AI 模型推理 |
| `GRAPHICS` | 图形叠加模块，绘制检测框、文本等 |
| `OUTPUT` | 输出模块，输出到 Rockit、文件或应用层 |
| `VOICE` | 语音处理模块，如 VAD |
| `BUSINESS` | 业务逻辑模块，如数据分流、合并、开关 |

### Rockit 模块类型

| 模块名 | 说明 |
|--------|------|
| `VI` | 视频输入模块 |
| `VPSS` | 视频处理子系统模块 |
| `VO` | 视频输出模块 |
| `AI` | 音频输入模块 |
| `RGN` | 区域叠加模块 |

### 通用模块结构

每个模块都遵循相似的结构：

```yaml
modules:
  - name: "MODULE_NAME"
    # 设备/组配置（可选，Rockit 模块常用）
    devices/groups:
      - dev_id/grp_id: 0
        dev_config/grp_config: {...}
        # 该设备/组下的通道列表
        pipes/layers/channels:
          - pipe_id/layer_id/chn_id: 0
            pipe_config/layer_config/chn_config: {...}
    # 通道列表（RKNNStream 模块常用）
    channels:
      - chn_id: 0
        chn_config: {...}
```

### 绑定关系

模块之间通过 `bindings` 连接：

```yaml
bindings:
  - name: "input_to_infer"
    src_module: "INPUT"
    src_dev_id: 0
    src_chn_id: 0
    dst_module: "INFER"
    dst_dev_id: 0
    dst_chn_id: 0
```

### Attach 关系（Rockit 专用）

RGN 等模块使用 `attaches` 附加到 VO：

```yaml
attaches:
  - name: "rgn_to_vo"
    rgn_handle: 0
    dst_module: "VO"
    dst_dev_id: 7
    dst_chn_id: 0
```

### 完整配置示例

详细示例请参考 `rkns_yolo_config.yaml` 和 `rkns_asr_config.yaml`。

## Launch 软件结构

### 核心数据结构

#### RKNSLaunchConfig - 全局配置

```c
typedef struct _RKNSLaunchConfig {
    // 运行时配置
    RKNS_S32 timeout;
    RKNS_BOOL enableProfiling;
    RKNS_BOOL verbose;

    // Pipeline 配置数组
    RKNSLaunchPipelineDesc pipelines[MAX_PIPELINES];
    RKNS_U32 pipelineCount;
} RKNSLaunchConfig;
```

#### RKNSLaunchPipelineDesc - 流水线描述

```c
typedef struct _RKNSLaunchPipelineDesc {
    char name[MAX_PIPELINE_NAME];
    RKNSLaunchPipelineType type;
    RKNS_BOOL enabled;

    RKNSLaunchModule modules[MAX_MODULES];
    RKNS_U32 moduleCount;

    RKNSLaunchBinding bindings[MAX_BINDINGS];
    RKNS_U32 bindingCount;

    RKNSLaunchAttach attaches[MAX_ATTACHES];
    RKNS_U32 attachCount;
} RKNSLaunchPipelineDesc;
```

#### RKNSLaunchModule - 模块描述

```c
typedef struct _RKNSLaunchModule {
    char name[MAX_MODULE_NAME];
    RKNSLaunchModId modId;
    RKNS_S32 devId;
    RKNS_S32 chnId;

    // Rockit 扩展字段
    RKNS_S32 pipeId;
    RKNS_S32 layerId;

    // 模块特有配置
    RKNS_BOOL configParsed;
    RKNS_U8 configBuf[RKNS_MODULE_CONFIG_SIZE];

    // 次级配置
    RKNS_BOOL hasSecondaryConfig;
    RKNS_U8 secondaryConfigBuf[RKNS_MODULE_SECONDARY_SIZE];
} RKNSLaunchModule;
```

### 模块操作函数表

每个模块类型都需要实现以下操作函数：

```c
typedef struct _RKNSLaunchModuleOps {
    RKNSLaunchModId modId;
    const char *name;

    RKNS_S32 (*parse_config)(YamlDocument *doc, const char *base_path,
                             struct _RKNSLaunchModule *module);
    RKNS_S32 (*create)(const struct _RKNSLaunchModule *module,
                       struct _RKNSLaunchModuleState *state);
    RKNS_S32 (*destroy)(const struct _RKNSLaunchModule *module,
                        struct _RKNSLaunchModuleState *state);
    RKNS_S32 (*start)(const struct _RKNSLaunchModule *module,
                      struct _RKNSLaunchModuleState *state);
    RKNS_S32 (*stop)(const struct _RKNSLaunchModule *module,
                     struct _RKNSLaunchModuleState *state);
} RKNSLaunchModuleOps;
```

### 程序执行流程

1. **初始化**：解析命令行参数或加载 YAML 配置
2. **模块创建**：按顺序创建各模块
3. **绑定建立**：建立模块间的绑定关系
4. **启动流水线**：按顺序启动各模块
5. **主循环**：等待超时或用户中断
6. **停止流水线**：按逆序停止各模块
7. **销毁模块**：按逆序销毁各模块
8. **清理退出**

## 如何添加一个模块

### 步骤 1：定义配置结构（如需要）

在 `rkns_launch_config.h` 中定义模块特有的配置结构体：

```c
typedef struct _MyModuleConfig {
    RKNS_S32 param1;
    RKNS_FLOAT param2;
    // ... 其他参数
} MyModuleConfig;
```

### 步骤 2：实现模块操作函数

在 `rkns_launch.c` 中实现五个操作函数：

```c
RKNS_S32 mymodule_parse_config(YamlDocument *doc, const char *base_path,
                               RKNSLaunchModule *module) {
    MyModuleConfig *cfg = MODULE_CONFIG(module, MyModuleConfig);
    char path[512];

    rkos_memset(cfg, 0, sizeof(MyModuleConfig));

    // 解析配置参数
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.param1", base_path);
    cfg->param1 = yaml_get_int(doc, path, 0);

    snprintf(path, sizeof(path), "%s.channels.0.chn_config.param2", base_path);
    cfg->param2 = (RKNS_FLOAT)yaml_get_float(doc, path, 0.0f);

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 mymodule_create(const RKNSLaunchModule *module,
                         RKNSLaunchModuleState *state) {
    // 调用 RKNNStream API 创建模块
    // ...
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 mymodule_destroy(const RKNSLaunchModule *module,
                          RKNSLaunchModuleState *state) {
    if (state->created) {
        // 调用 RKNNStream API 销毁模块
        // ...
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 mymodule_start(const RKNSLaunchModule *module,
                        RKNSLaunchModuleState *state) {
    // 调用 RKNNStream API 启动模块
    // ...
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 mymodule_stop(const RKNSLaunchModule *module,
                       RKNSLaunchModuleState *state) {
    if (state->started) {
        // 调用 RKNNStream API 停止模块
        // ...
        state->started = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}
```

### 步骤 3：注册模块操作函数

在 [rkns_launch.c](file:///home/rockchip/rockit_repo/samples/rknnstream_launch/rkns_launch.c#L3722-L3748) 中的模块操作函数表 `g_moduleOps` 中添加：

```c
static const RKNSLaunchModuleOps g_moduleOps[] = {
    /* RKNS 模块 - 直接用原始 ID (RKNS_ID_XXX)
    { RKNS_ID_INPUT,    "INPUT",    input_parse_config,
      input_create,    input_destroy,    input_start,    input_stop    },
    { RKNS_ID_INFER,    "INFER",    infer_parse_config,
      infer_create,    infer_destroy,    infer_start,    infer_stop    },
    { RKNS_ID_GRAPHICS, "GRAPHICS", graphics_parse_config,
      graphics_create, graphics_destroy, graphics_start, graphics_stop },
    { RKNS_ID_OUTPUT,   "OUTPUT",   output_parse_config,
      output_create,   output_destroy,   output_start,   output_stop   },
    { RKNS_ID_VOICE,    "VOICE",    voice_parse_config,
      voice_create,    voice_destroy,    voice_start,    voice_stop    },
    { RKNS_ID_BUSINESS, "BUSINESS", business_parse_config,
      business_create, business_destroy, business_start, business_stop },
    /* Rockit 模块 - 原始 ID (RK_ID_XXX) + 偏移
    { RK_ID_VI + RKNS_LAUNCH_ROCKIT_ID_OFFSET,    "VI",       rockit_vi_parse_config,
      rockit_vi_create,    rockit_vi_destroy,    rockit_vi_start,    rockit_vi_stop },
    { RK_ID_VO + RKNS_LAUNCH_ROCKIT_ID_OFFSET,    "VO",       rockit_vo_parse_config,
      rockit_vo_create,    rockit_vo_destroy,    rockit_vo_start,    rockit_vo_stop },
    { RK_ID_VPSS + RKNS_LAUNCH_ROCKIT_ID_OFFSET,  "VPSS",     rockit_vpss_parse_config,
      rockit_vpss_create,  rockit_vpss_destroy,  rockit_vpss_start,  rockit_vpss_stop },
    { RK_ID_RGN + RKNS_LAUNCH_ROCKIT_ID_OFFSET,   "RGN",      rockit_rgn_parse_config,
      rockit_rgn_create,   rockit_rgn_destroy,   rockit_rgn_start,   rockit_rgn_stop },
    { RK_ID_AI + RKNS_LAUNCH_ROCKIT_ID_OFFSET,    "AI",       rockit_ai_parse_config,
      rockit_ai_create,    rockit_ai_destroy,    rockit_ai_start,    rockit_ai_stop },
    
    // 添加新模块
    { RKNS_ID_MYMODULE, "MYMODULE", mymodule_parse_config,
      mymodule_create, mymodule_destroy, mymodule_start, mymodule_stop },
};
```

### 步骤 4：在 YAML 中使用模块

```yaml
modules:
  - name: "MYMODULE"
    channels:
      - chn_id: 0
        chn_config:
          param1: 100
          param2: 0.5
```

## 如何添加一个 PIPE 流

### 方法 1：在 YAML 中添加新 Pipeline

在 `pipelines` 数组中添加新的流水线配置：

```yaml
pipelines:
  # 已有流水线...

  # 新增流水线
  - name: "my_new_pipeline"
    type: "rknnstream"  # 或 "rockit"
    enabled: true

    modules:
      # 配置你的模块
      - name: "INPUT"
        channels: [...]
      - name: "INFER"
        channels: [...]
      - name: "OUTPUT"
        channels: [...]

    bindings:
      # 配置模块绑定关系
      - name: "input_to_infer"
        src_module: "INPUT"
        src_dev_id: 0
        src_chn_id: 0
        dst_module: "INFER"
        dst_dev_id: 0
        dst_chn_id: 0
      - name: "infer_to_output"
        src_module: "INFER"
        src_dev_id: 0
        src_chn_id: 0
        dst_module: "OUTPUT"
        dst_dev_id: 0
        dst_chn_id: 0
```

### 方法 2：在现有 Pipeline 中添加新通道

为模块添加多个通道配置：

```yaml
modules:
  - name: "INFER"
    channels:
      - chn_id: 0  # 已有通道
        chn_config: {...}
      - chn_id: 1  # 新增通道
        chn_config: {...}
```

然后添加相应的绑定：

```yaml
bindings:
  # 已有绑定...
  - name: "input_to_infer_chn1"
    src_module: "INPUT"
    src_dev_id: 0
    src_chn_id: 1
    dst_module: "INFER"
    dst_dev_id: 0
    dst_chn_id: 1
```

### 混合流水线示例

结合 RKNNStream 和 Rockit 的混合流水线：

```yaml
pipelines:
  # Rockit 视频采集流水线
  - name: "rockit_video_pipeline"
    type: "rockit"
    enabled: true
    modules:
      - name: "VI"
        devices: [...]
      - name: "VPSS"
        groups: [...]
      - name: "VO"
        devices: [...]
    bindings: [...]
    attaches: [...]

  # RKNNStream AI 推理流水线
  - name: "rknnstream_ai_pipeline"
    type: "rknnstream"
    enabled: true
    modules:
      - name: "INPUT"
        channels:
          - chn_id: 0
            chn_config:
              type: "ROCKIT"
              op_type: "VPSS_CAPTURE"
              source:
                module: "VPSS"
                dev_id: 0
                chn_id: 1
      - name: "INFER"
        channels: [...]
      - name: "GRAPHICS"
        channels: [...]
      - name: "OUTPUT"
        channels: [...]
    bindings: [...]
```
