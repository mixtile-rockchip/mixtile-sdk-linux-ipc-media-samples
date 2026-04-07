// Copyright (c) 2026 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file rkns_launch_config.h
 * @brief RKNNStream Launch 配置管理和 YAML 解析
 */

#ifndef _RKNS_LAUNCH_CONFIG_H_
#define _RKNS_LAUNCH_CONFIG_H_

#include "rkns.h"
#include "rkns_api_input.h"
#include "rkns_api_infer.h"
#include "rkns_api_output.h"
#include "rkns_api_graphics.h"
#include "yaml_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 常量定义
 * ============================================================ */

#define MAX_MODEL_PATH      256
#define MAX_PATH            256
#define MAX_MODULE_NAME     32
#define MAX_BINDING_NAME    64
#define MAX_ATTACH_NAME     64
#define MAX_MODULES         16
#define MAX_BINDINGS        16
#define MAX_ATTACHES        8
#define MAX_PIPELINE_NAME   64
#define MAX_PIPELINES       4

/* ============================================================
 * 统一模块 ID（解决 RKNS/Rockit 模块 ID 冲突）
 * ============================================================ */

/**
 * @brief Launch 统一模块 ID
 *
 * RKNS 模块保持与 RKNS_MOD_ID_E 一致的值（0-14），
 * Rockit 模块从 100 开始避免冲突。
 */
typedef enum _RKNSLaunchModId {
    /* RKNS 模块 */
    RKNS_LAUNCH_MOD_RKNS      = 0,
    RKNS_LAUNCH_MOD_MB        = 1,
    RKNS_LAUNCH_MOD_SYS       = 2,
    RKNS_LAUNCH_MOD_COMPOSE   = 3,
    RKNS_LAUNCH_MOD_EMBEDDING = 4,
    RKNS_LAUNCH_MOD_GRAPHICS  = 5,
    RKNS_LAUNCH_MOD_IMAGE     = 6,
    RKNS_LAUNCH_MOD_INFER     = 7,
    RKNS_LAUNCH_MOD_INPUT     = 8,
    RKNS_LAUNCH_MOD_OUTPUT    = 9,
    RKNS_LAUNCH_MOD_TEXT      = 10,
    RKNS_LAUNCH_MOD_VOICE     = 11,
    RKNS_LAUNCH_MOD_LOG       = 12,
    /* Rockit 模块 */
    RKNS_LAUNCH_MOD_RK_VI     = 100,
    RKNS_LAUNCH_MOD_RK_VO     = 101,
    RKNS_LAUNCH_MOD_RK_VPSS   = 102,
    RKNS_LAUNCH_MOD_RK_RGN    = 103,
    RKNS_LAUNCH_MOD_RK_AI     = 104,
    RKNS_LAUNCH_MOD_BUTT,
} RKNSLaunchModId;

/* Rockit 模块配置结构体（存储在 configBuf 中） */
typedef struct _RockitVIConfig {
    RKNS_S32 devId;
    RKNS_S32 pipeId;
    RKNS_S32 chnId;
    RKNS_U32 width;
    RKNS_U32 height;
    RKNS_U32 frameRate;
    RKNS_U32 bufferCount;
    RKNS_U32 depth;
} RockitVIConfig;

typedef struct _RockitVOConfig {
    RKNS_S32 devId;
    RKNS_S32 layerId;
    RKNS_S32 chnId;
    RKNS_U32 layerWidth;
    RKNS_U32 layerHeight;
    RKNS_U32 dispWidth;
    RKNS_U32 dispHeight;
    RKNS_S32 intfType;
} RockitVOConfig;

/* AI 通道声道映射（通道声道到声卡物理声道） */
#define MAX_CHN_CHNS 8
typedef struct _RockitAIChnMap {
    RKNS_U32 chnCount;           // 通道的声道数（1=单声道，2=立体声等）
    RKNS_U32 chnMap[MAX_CHN_CHNS]; // 每个通道声道映射到的声卡物理声道索引
} RockitAIChnMap;

/* AI 通道配置（单个通道） */
typedef struct _RockitAIChnConfig {
    RKNS_S32 chnId;
    RKNS_U32 sampleRate;
    RKNS_U32 channels;
    RKNS_U32 bitWidth;
    RKNS_U32 frameTime;       // 帧时间（毫秒为单位）
    RKNS_U32 bufferCount;
    RKNS_U32 depth;
    /* 声道映射配置 */
    RockitAIChnMap channelMap; // 通道声道到声卡声道的映射
    /* 轨道模式（当声卡是2声道但通道是1声道时的处理方式） */
    RKNS_S32 trackMode; // AUDIO_TRACK_* 枚举值
    /* VQE 配置 */
    char vqeConfigPath[256]; // VQE 配置文件路径
} RockitAIChnConfig;

/* AI 声卡设备配置（包含通道列表） */
typedef struct _RockitAIDevConfig {
    RKNS_S32 devId;
    /* 声卡参数 */
    RKNS_U32 sampleRate;
    RKNS_U32 channels;
    RKNS_U32 bitWidth;
    char soundCard[64];
    /* 通道映射使能开关（true=启用多通道+声道映射，false=单声道模式） */
    RKNS_BOOL channelMapEnable;
    /* 该设备下的通道列表（动态分配） */
    RockitAIChnConfig* chnConfigs;
    RKNS_U32 chnCount;
} RockitAIDevConfig;

/* AI 声卡配置（包含多个设备） */
typedef struct _RockitAIConfig {
    /* 设备列表（动态分配） */
    RockitAIDevConfig* devConfigs;
    RKNS_U32 devCount;
} RockitAIConfig;

/**
 * @brief 应用场景类型
 */
typedef enum _RKNSLaunchScenario {
    RKNS_LAUNCH_SCENARIO_DETECTION = 0,   // 目标检测
    RKNS_LAUNCH_SCENARIO_FACE,            // 人脸识别
    RKNS_LAUNCH_SCENARIO_SEGMENT,         // 视频分割
    RKNS_LAUNCH_SCENARIO_TRACK,           // 目标跟踪
    RKNS_LAUNCH_SCENARIO_CUSTOM,          // 自定义场景
    RKNS_LAUNCH_SCENARIO_MAX
} RKNSLaunchScenario;

/**
 * @brief Pipeline 模块描述
 *
 * 包含模块基础信息（name/devId/chnId）以及从 YAML modules[].config
 * 解析的模块特有配置参数（存储在 configBuf 中）。
 */
#define RKNS_MODULE_CONFIG_SIZE      512
#define RKNS_MODULE_SECONDARY_SIZE   8192

#define MODULE_CONFIG(mod, type)       ((type *)((mod)->configBuf))
#define MODULE_SECONDARY(mod, type)    ((type *)((mod)->secondaryConfigBuf))

typedef struct _RKNSLaunchModule {
    char name[MAX_MODULE_NAME];
    RKNSLaunchModId modId;
    RKNS_S32 devId;
    RKNS_S32 chnId;

    /* Rockit 扩展字段 */
    RKNS_S32 pipeId;   /* Rockit VI pipe ID */
    RKNS_S32 layerId;  /* Rockit VO layer ID */

    /* 模块特有配置（从 YAML modules[].config 解析） */
    RKNS_BOOL configParsed;
    RKNS_U8 configBuf[RKNS_MODULE_CONFIG_SIZE];

    /* 次级配置（如 GRAPHICS 的 overlayRule） */
    RKNS_BOOL hasSecondaryConfig;
    RKNS_U8 secondaryConfigBuf[RKNS_MODULE_SECONDARY_SIZE];
} RKNSLaunchModule;

/* 前向声明 */
struct _RKNSLaunchModule;
struct _RKNSLaunchModuleState;

/**
 * @brief 模块操作函数表
 *
 * 每个模块类型注册一组操作函数，通过 rkns_launch_find_module_ops() 查找。
 */
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

/**
 * @brief AI设备/通道启用状态（支持多设备多通道）
 */
#define MAX_AI_DEVICES 4
#define MAX_AI_CHANNELS_PER_DEV 8
typedef struct _RKNSLaunchAIState {
    RKNS_BOOL devEnabled[MAX_AI_DEVICES];
    RKNS_BOOL chnEnabled[MAX_AI_DEVICES][MAX_AI_CHANNELS_PER_DEV];
    RKNS_U32 devCount;
    RKNS_U32 chnCount[MAX_AI_DEVICES];
} RKNSLaunchAIState;

/**
 * @brief 模块运行时状态
 */
typedef struct _RKNSLaunchModuleState {
    RKNS_CHN_S chn;
    RKNS_BOOL created;
    RKNS_BOOL started;
    /* Rockit 模块运行时状态 */
    RKNS_BOOL viDevEnabled;
    RKNS_BOOL viChnEnabled;
    RKNS_BOOL voDevEnabled;
    RKNS_BOOL voLayerEnabled;
    RKNS_BOOL voLayerBound;
    /* AI模块使用新的多设备多通道状态 */
    RKNSLaunchAIState aiState;
} RKNSLaunchModuleState;

/**
 * @brief Pipeline 绑定关系
 */
typedef struct _RKNSLaunchBinding {
    char name[MAX_BINDING_NAME];
    RKNS_CHN_S srcChn;
    RKNS_CHN_S dstChn;
} RKNSLaunchBinding;

/**
 * @brief Pipeline 框架类型
 */
typedef enum _RKNSLaunchPipelineType {
    RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM = 0,
    RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT,
    RKNS_LAUNCH_PIPELINE_TYPE_MAX
} RKNSLaunchPipelineType;

/**
 * @brief RGN attach 关系（Rockit 专用）
 */
typedef struct _RKNSLaunchAttach {
    char name[MAX_ATTACH_NAME];
    RKNS_S32 rgnHandle;
    RKNS_CHN_S dstChn;
} RKNSLaunchAttach;

/**
 * @brief 单条 Pipeline 描述
 *
 * 包含该 pipeline 的 name/type/enabled 及其 modules/bindings/attaches，
 * 完整对应 YAML pipelines[] 中的一个条目。
 */
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

/* ============================================================
 * 全局变量
 * ============================================================ */

/**
 * @brief 输入源类型字符串映射
 */
extern const char *g_inputTypeStr[];

/**
 * @brief 输出类型字符串映射
 */
extern const char *g_outputTypeStr[];

/**
 * @brief 模型类型字符串映射
 */
extern const char *g_modelTypeStr[];

/**
 * @brief 场景字符串映射
 */
extern const char *g_scenarioStr[];

/* ============================================================
 * 配置结构体
 * ============================================================ */

/**
 * @brief RKNNStream Launch 配置结构体
 */
typedef struct _RKNSLaunchConfig {
    // 场景配置
    RKNSLaunchScenario scenario;

    // 输入配置
    RKNSInputSourceType inputType;
    char inputPath[MAX_PATH];
    RKNS_U32 width;
    RKNS_U32 height;
    RKNS_U32 frameRate;

    // 推理配置
    RKNS_MODEL_TYPE_E modelType;
    char modelPath[MAX_MODEL_PATH];
    RKNS_FLOAT confidenceThreshold;
    RKNS_U32 inputQueueCnt;
    RKNS_U32 outputBufferCnt;

    // Overlay 配置
    RKNS_BOOL enableOverlay;
    RKNS_OVERLAY_PIXEL_FMT_E pixelFormat;
    RKNS_OVERLAY_ATTR_S overlayAttr;
    RKNS_OVERLAY_RULE_S overlayRule;

    // 输出配置
    RKNSOutputType outputType;
    char outputPath[MAX_PATH];
    RKNS_S32 watchDepth;

    // Rockit 特定配置
    RKNSRockitCmd rockitCmd;
    RKNS_S32 rockitLayerId;
    RKNS_S32 rockitChnId;

    // 运行时配置
    RKNS_S32 timeout;
    RKNS_BOOL enableProfiling;
    RKNS_BOOL verbose;

    // Pipeline 配置（从 YAML pipelines[] 解析）
    RKNSLaunchPipelineDesc pipelines[MAX_PIPELINES];
    RKNS_U32 pipelineCount;
} RKNSLaunchConfig;

/* ============================================================
 * 配置管理函数
 * ============================================================ */

/**
 * @brief 初始化默认配置
 * @param config 配置结构体指针
 */
void init_default_config(RKNSLaunchConfig *config);

/**
 * @brief 打印配置信息
 * @param config 配置结构体指针
 */
void print_config(const RKNSLaunchConfig *config);

/**
 * @brief 从 YAML 文件加载配置
 * @param config 配置结构体指针
 * @param yaml_file YAML 配置文件路径
 * @return RKNS_SUCCESS 成功，RKNS_FAILURE 失败
 */
RKNS_S32 rkns_launch_load_config_from_yaml(RKNSLaunchConfig *config,
                                          const char *yaml_file);

/**
 * @brief 保存当前配置到 YAML 文件
 * @param config 配置结构体指针
 * @param yaml_file YAML 配置文件路径
 * @return RKNS_SUCCESS 成功，RKNS_FAILURE 失败
 */
RKNS_S32 rkns_launch_save_config_to_yaml(const RKNSLaunchConfig *config,
                                        const char *yaml_file);

/**
 * @brief 查找模块操作函数表
 * @param modId 模块 ID
 * @return 操作函数表指针，未找到返回 NULL
 */
const RKNSLaunchModuleOps *rkns_launch_find_module_ops(RKNSLaunchModId modId);

/**
 * @brief 判断统一模块 ID 是否属于 Rockit 框架
 */
RKNS_BOOL rkns_launch_is_rockit_module(RKNSLaunchModId id);

/**
 * @brief 将统一模块 ID 映射为 Rockit MOD_ID_E
 */
RKNS_S32 rkns_launch_to_mpp_id(RKNSLaunchModId id);

/**
 * @brief 将统一模块 ID 映射为 RKNS_MOD_ID_E
 */
RKNS_S32 rkns_launch_to_rkns_id(RKNSLaunchModId id);

/**
 * @brief 将 CLI 参数同步到指定 pipeline 的模块配置
 * @param config 配置结构体指针
 * @param pl 目标 pipeline 描述指针
 */
void sync_cli_to_pipeline_desc(RKNSLaunchConfig *config, RKNSLaunchPipelineDesc *pl);

/**
 * @brief 将指定 pipeline 的模块配置同步回扁平 config 字段（YAML 模式下使用）
 * @param pl 源 pipeline 描述指针
 * @param config 配置结构体指针
 */
void sync_pipeline_desc_to_cli(const RKNSLaunchPipelineDesc *pl, RKNSLaunchConfig *config);

/* 类型解析函数（供模块 parse_config 使用） */
RKNSInputSourceType parse_input_type(const char *type_str);
RKNSOutputType parse_output_type(const char *type_str);
RKNS_MODEL_TYPE_E parse_model_type(const char *type_str);
RKNS_OVERLAY_PIXEL_FMT_E parse_pixel_format(const char *fmt_str);

#ifdef __cplusplus
}
#endif

#endif /* _RKNS_LAUNCH_CONFIG_H_ */
