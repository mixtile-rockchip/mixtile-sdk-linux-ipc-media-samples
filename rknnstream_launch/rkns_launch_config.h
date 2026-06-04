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
#include "rkns_api_text.h"
#include "rkns_api_business.h"
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

#ifndef MAX_VI_BIND_PIPE_NUM
#define MAX_VI_BIND_PIPE_NUM 16
#endif

/* ============================================================
 * 统一模块 ID（解决 RKNS/Rockit 模块 ID 冲突）
 * ============================================================ */

/**
 * @brief Rockit 模块 ID 偏移值
 *
 * Rockit 模块的原始 ID 加上该偏移值作为 Launch 层的模块 ID，
 * 以避免与 RKNS 模块 ID 冲突。
 */
#define RKNS_LAUNCH_ROCKIT_ID_OFFSET  1000

/**
 * @brief Launch 统一模块 ID
 *
 * 直接使用 RKNS 模块的原始 ID，Rockit 模块使用原始 ID + RKNS_LAUNCH_ROCKIT_ID_OFFSET
 */
typedef RKNS_S32 RKNSLaunchModId;

/* Rockit VI 层级配置（设备 > Pipe > 通道） */
typedef struct _RockitVIChnConfig {
    RKNS_S32 chnId;
    RKNS_U32 width;
    RKNS_U32 height;
    RKNS_U32 depth;
    RKNS_U32 bufferCount;
} RockitVIChnConfig;

typedef struct _RockitVIPipeConfig {
    RKNS_S32 pipeId;
    RockitVIChnConfig* chnConfigs;
    RKNS_U32 chnCount;
} RockitVIPipeConfig;

typedef struct _RockitVIDevConfig {
    RKNS_S32 devId;
    RKNS_U32 frameRate;
    RockitVIPipeConfig* pipeConfigs;
    RKNS_U32 pipeCount;
} RockitVIDevConfig;

typedef struct _RockitVIConfig {
    RockitVIDevConfig* devConfigs;
    RKNS_U32 devCount;
} RockitVIConfig;

/* Rockit VPSS 层级配置（Group > 通道） */
typedef struct _RockitVPSSChnConfig {
    RKNS_S32 chnId;
    RKNS_U32 width;
    RKNS_U32 height;
    RKNS_U32 depth;
    RKNS_U32 bufferCount;
    RKNS_U32 pixelFormat;  /* 像素格式，使用 PIXEL_FORMAT_E */
} RockitVPSSChnConfig;

typedef struct _RockitVPSSGrpConfig {
    RKNS_S32 grpId;
    RKNS_U32 maxWidth;
    RKNS_U32 maxHeight;
    RKNS_U32 frameRate;
    RockitVPSSChnConfig* chnConfigs;
    RKNS_U32 chnCount;
} RockitVPSSGrpConfig;

typedef struct _RockitVPSSConfig {
    RockitVPSSGrpConfig* grpConfigs;
    RKNS_U32 grpCount;
} RockitVPSSConfig;

/* Rockit VO 层级配置（Device > Layer > Channel） */
typedef struct _RockitVOChnConfig {
    RKNS_S32 chnId;
    RKNS_U32 width;
    RKNS_U32 height;
    RKNS_U32 priority;
    RKNS_U32 fgAlpha;
    RKNS_U32 bgAlpha;
} RockitVOChnConfig;

typedef struct _RockitVOLayerConfig {
    RKNS_S32 layerId;
    RKNS_U32 layerWidth;
    RKNS_U32 layerHeight;
    RKNS_U32 dispWidth;
    RKNS_U32 dispHeight;
    RKNS_S32 layerMode;
    RockitVOChnConfig* chnConfigs;
    RKNS_U32 chnCount;
} RockitVOLayerConfig;

typedef struct _RockitVODevConfig {
    RKNS_S32 devId;
    RKNS_S32 intfType;
    RKNS_S32 intfSync;
    RKNS_U32 bgColor;
    RockitVOLayerConfig* layerConfigs;
    RKNS_U32 layerCount;
} RockitVODevConfig;

typedef struct _RockitVOConfig {
    RockitVODevConfig* devConfigs;
    RKNS_U32 devCount;
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
 * @brief RockX 图形检测引擎配置结构
 */
typedef struct _RockitRGNConfig {
    unsigned int w;              /* 宽度 */
    unsigned int h;              /* 高度 */
    unsigned int fmt;            /* 像素格式 */
    int vo_dev_id;               /* 绑定的 VO 设备 ID */
    int vo_chn_id;               /* 绑定的 VO 通道 ID */
} RockitRGNConfig;

/* ============================================================
 * INFER 模块多通道配置
 * ============================================================ */

/**
 * @brief 单个 INFER 通道配置
 */
typedef struct _InferChannelConfig {
    RKNS_S32 chnId;
    RKNS_INFER_ATTR_S attr;      /* INFER 通道属性 */
    char *strPool;               /* 字符串池，供 attr 中的 char* 使用 */
    size_t strPoolSize;          /* 字符串池大小 */
} InferChannelConfig;

/**
 * @brief INFER 模块多通道配置
 */
typedef struct _InferConfig {
    InferChannelConfig *chnConfigs;  /* 通道配置列表 */
    RKNS_U32 chnCount;               /* 通道数量 */
} InferConfig;

/**
 * @brief INFER 模块多通道状态
 */
typedef struct _RKNSLaunchInferState {
    RKNS_BOOL *chnEnabled;      /* 通道启用状态数组（动态分配） */
    RKNS_U32 chnCount;          /* 通道数量 */
} RKNSLaunchInferState;

/* ============================================================
 * OUTPUT 模块多通道配置
 * ============================================================ */

/**
 * @brief 单个 OUTPUT 通道配置
 */
typedef struct _OutputChannelConfig {
    RKNS_S32 chnId;
    RKNSOutputParam param;      /* OUTPUT 通道属性 */
    char *strPool;              /* 字符串池，供 param 中的 char* 使用 */
    size_t strPoolSize;         /* 字符串池大小 */
} OutputChannelConfig;

/**
 * @brief OUTPUT 模块多通道配置
 */
typedef struct _OutputConfig {
    OutputChannelConfig *chnConfigs;  /* 通道配置列表 */
    RKNS_U32 chnCount;                /* 通道数量 */
} OutputConfig;

/**
 * @brief OUTPUT 模块多通道状态
 */
typedef struct _RKNSLaunchOutputState {
    RKNS_BOOL *chnEnabled;      /* 通道启用状态数组（动态分配） */
    RKNS_U32 chnCount;          /* 通道数量 */
} RKNSLaunchOutputState;

/* ============================================================
 * BUSINESS 模块多通道/组配置
 * ============================================================ */

/**
 * @brief 单个 BUSINESS 通道配置
 */
typedef struct _BusinessChannelConfig {
    RKNS_S32 chnId;             /* 通道 ID */
    char *strPool;              /* 字符串池，供 param 中的 char* 使用 */
    size_t strPoolSize;         /* 字符串池大小 */
} BusinessChannelConfig;

/**
 * @brief 单个 BUSINESS 组配置
 */
typedef struct _BusinessGroupConfig {
    RKNS_S32 grpId;             /* 组 ID */
    RKNSBusinessParam param;    /* BUSINESS 组属性 */
    char *strPool;              /* 字符串池，供 param 中的 char* 使用 */
    size_t strPoolSize;         /* 字符串池大小 */
    BusinessChannelConfig *chnConfigs;  /* 该组的通道配置列表 */
    RKNS_U32 chnCount;                  /* 该组的通道数量 */
} BusinessGroupConfig;

/**
 * @brief BUSINESS 模块多组配置
 */
typedef struct _BusinessConfig {
    BusinessGroupConfig *grpConfigs;  /* 组配置列表 */
    RKNS_U32 grpCount;                  /* 组数量 */
} BusinessConfig;

/**
 * @brief BUSINESS 模块多组状态
 */
typedef struct _RKNSLaunchBusinessState {
    RKNS_BOOL *grpEnabled;      /* 组启用状态数组（动态分配） */
    RKNS_BOOL **chnEnabled;     /* 通道启用状态数组（动态分配，grpCount x chnCount） */
    RKNS_U32 grpCount;          /* 组数量 */
    RKNS_U32 *chnCount;         /* 每个组的通道数量 */
} RKNSLaunchBusinessState;


/* ============================================================
 * TEXT 模块多通道/组配置
 * ============================================================ */

/**
 * @brief 单个 TEXT 通道配置
 */
typedef struct _TextChannelConfig {
    RKNS_S32 chnId;             /* 通道 ID */
    char *strPool;              /* 字符串池，供 param 中的 char* 使用 */
    size_t strPoolSize;         /* 字符串池大小 */
} TextChannelConfig;

/**
 * @brief 单个 TEXT 组配置
 */
typedef struct _TextGroupConfig {
    RKNS_S32 grpId;             /* 组 ID */
    RKNSTextParam param;        /* TEXT 组属性 */
    char *strPool;              /* 字符串池，供 param 中的 char* 使用 */
    size_t strPoolSize;         /* 字符串池大小 */
    TextChannelConfig *chnConfigs;  /* 该组的通道配置列表 */
    RKNS_U32 chnCount;                  /* 该组的通道数量 */
} TextGroupConfig;

/**
 * @brief TEXT 模块多组配置
 */
typedef struct _TextConfig {
    TextGroupConfig *grpConfigs;  /* 组配置列表 */
    RKNS_U32 grpCount;                  /* 组数量 */
} TextConfig;

/**
 * @brief TEXT 模块多组状态
 */
typedef struct _RKNSLaunchTextState {
    RKNS_BOOL *grpEnabled;      /* 组启用状态数组（动态分配） */
    RKNS_BOOL **chnEnabled;     /* 通道启用状态数组（动态分配，grpCount x chnCount） */
    RKNS_U32 grpCount;          /* 组数量 */
    RKNS_U32 *chnCount;         /* 每个组的通道数量 */
} RKNSLaunchTextState;

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
 * @brief VI 设备/通道启用状态（支持多设备多 Pipe 多通道）
 */
#define MAX_VI_DEVICES    4
#define MAX_VI_PIPES      4
#define MAX_VI_CHNS       4
typedef struct _RKNSLaunchVIState {
    RKNS_BOOL devEnabled[MAX_VI_DEVICES];
    RKNS_BOOL chnEnabled[MAX_VI_DEVICES][MAX_VI_PIPES][MAX_VI_CHNS];
    RKNS_U32 devCount;
    RKNS_U32 pipeCount[MAX_VI_DEVICES];
    RKNS_U32 chnCount[MAX_VI_DEVICES][MAX_VI_PIPES];
} RKNSLaunchVIState;

/**
 * @brief VPSS Group/通道启用状态
 */
#define MAX_VPSS_GROUPS    4
#define MAX_VPSS_CHNS      4
typedef struct _RKNSLaunchVPSSState {
    RKNS_BOOL grpEnabled[MAX_VPSS_GROUPS];
    RKNS_BOOL chnEnabled[MAX_VPSS_GROUPS][MAX_VPSS_CHNS];
    RKNS_U32 grpCount;
    RKNS_U32 chnCount[MAX_VPSS_GROUPS];
} RKNSLaunchVPSSState;

/**
 * @brief VO 设备/图层/通道启用状态
 */
#define MAX_VO_DEVICES      4
#define MAX_VO_LAYERS      4
#define MAX_VO_CHNS        4
typedef struct _RKNSLaunchVOState {
    RKNS_BOOL devEnabled[MAX_VO_DEVICES];
    RKNS_BOOL layerBound[MAX_VO_DEVICES][MAX_VO_LAYERS];
    RKNS_BOOL layerEnabled[MAX_VO_DEVICES][MAX_VO_LAYERS];
    RKNS_BOOL chnEnabled[MAX_VO_DEVICES][MAX_VO_LAYERS][MAX_VO_CHNS];
    RKNS_U32 devCount;
    RKNS_U32 layerCount[MAX_VO_DEVICES];
    RKNS_U32 chnCount[MAX_VO_DEVICES][MAX_VO_LAYERS];
} RKNSLaunchVOState;

/**
 * @brief 模块运行时状态
 */
typedef struct _RKNSLaunchModuleState {
    RKNS_CHN_S chn;
    RKNS_BOOL created;
    RKNS_BOOL started;
    RKNSLaunchVIState viState;
    RKNSLaunchVPSSState vpssState;
    RKNSLaunchVOState voState;
    RKNSLaunchAIState aiState;
    RKNSLaunchInferState inferState;
    RKNSLaunchOutputState outputState;
    RKNSLaunchBusinessState bizState;
    RKNSLaunchTextState textState;
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
 * @brief UI 交互配置结构体
 */
typedef struct _RKNSLaunchUIConfig {
    // 是否启用 UI 交互
    RKNS_BOOL enabled;
    
    // 发送数据的模块配置
    char sendModuleName[MAX_MODULE_NAME];
    RKNS_S32 sendDevId;
    RKNS_S32 sendChnId;
    
    // 接收数据的模块配置
    char recvModuleName[MAX_MODULE_NAME];
    RKNS_S32 recvDevId;
    RKNS_S32 recvChnId;
} RKNSLaunchUIConfig;

/**
 * @brief RKNNStream Launch 配置结构体
 */
typedef struct _RKNSLaunchConfig {
    // 运行时配置
    RKNS_S32 timeout;
    RKNS_BOOL enableProfiling;
    RKNS_BOOL verbose;

    // UI 交互配置
    RKNSLaunchUIConfig uiConfig;

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
RockXPixelFormat parse_pixel_format(const char *fmt_str);

#ifdef __cplusplus
}
#endif

#endif /* _RKNS_LAUNCH_CONFIG_H_ */
