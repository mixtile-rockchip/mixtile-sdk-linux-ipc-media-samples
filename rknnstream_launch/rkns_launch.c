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
 * @file rkns_launch.c
 * @brief RKNNStream 完整应用演示程序
 *
 * 本程序演示如何使用 RKNNStream 框架构建完整的 AI 应用流水线，
 * 支持多种场景：目标检测、人脸识别、视频分割、目标跟踪等
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include "rkns.h"
#include "rk_osal.h"
#include "rkns_launch_config.h"

/* Rockit MPI headers */
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_ai.h"
#include "rk_comm_aio.h"
#include "rk_common.h"

/* ============================================================
 * 常量定义
 * ============================================================ */

#define DEV_ID              0
#define CHN_ID              0
#define MAX_MODEL_PATH      256
#define MAX_PATH            256
#define DEFAULT_TIMEOUT     0  // 0 表示无限运行

/* ============================================================
 * 配置结构体和全局变量（已在 rkns_launch_config.h 中定义）
 * ============================================================ */

/* ============================================================
 * 全局变量
 * ============================================================ */

static volatile RKNS_BOOL g_running = RKNS_TRUE;
static volatile RKNS_BOOL g_exit = RKNS_FALSE;
static RKNSLaunchConfig g_config;
static struct termios g_old_termios;

/* ============================================================
 * 工具函数
 * ============================================================ */

/**
 * @brief 信号处理函数
 */
static void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\n[INFO] Received SIGINT, exiting...\n");
        g_running = RKNS_FALSE;
        g_exit = RKNS_TRUE;
    }
}

/**
 * @brief 设置终端为非阻塞模式
 */
static int set_nonblocking(int fd) {
    struct termios newt;
    int flags;

    tcgetattr(fd, &g_old_termios);
    newt = g_old_termios;

    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(fd, TCSANOW, &newt);

    flags = fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

/**
 * @brief 恢复终端设置
 */
static void restore_terminal(int fd) {
    tcsetattr(fd, TCSANOW, &g_old_termios);
    int flags = fcntl(fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
}

/* ============================================================
 * 配置管理函数（已在 rkns_launch_config.c 中实现）
 * ============================================================ */

/**
 * @brief 打印帮助信息
 */
static void print_usage(const char *progName) {
    printf("Usage: %s [OPTIONS]\n\n", progName);
    printf("RKNNStream 完整应用演示程序\n\n");
    printf("Options:\n");
    printf("  -C, --config <file>         YAML 配置文件路径\n");
    printf("  -s, --scenario <type>       应用场景:\n");
    printf("                             - detection (目标检测)\n");
    printf("                             - face (人脸识别)\n");
    printf("                             - segment (视频分割)\n");
    printf("                             - track (目标跟踪)\n");
    printf("                             - custom (自定义)\n");
    printf("  -i, --input <type>          输入类型:\n");
    printf("                             - ROCKIT\n");
    printf("                             - GSTREAMER\n");
    printf("                             - OPENCV\n");
    printf("                             - CUSTOM\n");
    printf("  --input-path <path>         输入路径（设备文件或管道）\n");
    printf("  -W, --width <width>         输入宽度 [默认: 640]\n");
    printf("  -H, --height <height>       输入高度 [默认: 480]\n");
    printf("  -F, --fps <fps>             帧率 [默认: 30]\n");
    printf("  -m, --model <type>          模型类型:\n");
    printf("                             - ALM, CLASSIFY, DETECT\n");
    printf("                             - LLM, SEGMENT, TRACK\n");
    printf("                             - TTA, VLM\n");
    printf("  --model-path <path>         模型文件路径\n");
    printf("  -c, --confidence <value>    置信度阈值 [默认: 0.5]\n");
    printf("  --enable-overlay            启用 OSD 绘制\n");
    printf("  --disable-overlay           禁用 OSD 绘制\n");
    printf("  -o, --output <type>         输出类型:\n");
    printf("                             - APP, ROCKIT, GSTREAM, OPENCV\n");
    printf("  --output-path <path>        输出路径\n");
    printf("  --watch-depth <depth>       输出观察深度 [默认: 1]\n");
    printf("  -t, --timeout <seconds>     运行超时（0 = 无限） [默认: 0]\n");
    printf("  --profiling                 启用性能分析\n");
    printf("  -v, --verbose               详细输出\n");
    printf("  -h, --help                  显示此帮助信息\n");
    printf("\nExamples:\n");
    printf("  # 使用 YAML 配置文件（推荐）\n");
    printf("  %s --config rkns_launch_config.yaml\n", progName);
    printf("\n");
    printf("  # 目标检测演示\n");
    printf("  %s -s detection -i CUSTOM -m DETECT\n", progName);
    printf("\n");
    printf("  # 使用 Rockit 的完整流水线\n");
    printf("  %s -s detection -i ROCKIT -o ROCKIT --enable-overlay\n", progName);
    printf("\n");
    printf("  # 人脸识别演示\n");
    printf("  %s -s face -i OPENCV --input-path /dev/video0\n", progName);
    printf("\n");
    printf("  # 视频分割演示\n");
    printf("  %s -s segment -i GSTREAMER --input-pipeline \"...\"\n", progName);
}

/**
 * @brief 解析命令行参数
 */
static RKNS_S32 parse_arguments(RKNSLaunchConfig *config, int argc, char *argv[]) {
    int opt;
    int ret;
    const char *short_opts = "C:s:i:W:H:F:m:c:o:t:vh";
    struct option long_opts[] = {
        {"config", required_argument, 0, 'C'},
        {"scenario", required_argument, 0, 's'},
        {"input", required_argument, 0, 'i'},
        {"input-path", required_argument, 0, 0x100},
        {"width", required_argument, 0, 'W'},
        {"height", required_argument, 0, 'H'},
        {"fps", required_argument, 0, 'F'},
        {"model", required_argument, 0, 'm'},
        {"model-path", required_argument, 0, 0x101},
        {"confidence", required_argument, 0, 'c'},
        {"enable-overlay", no_argument, 0, 0x102},
        {"disable-overlay", no_argument, 0, 0x103},
        {"output", required_argument, 0, 'o'},
        {"output-path", required_argument, 0, 0x104},
        {"watch-depth", required_argument, 0, 0x105},
        {"timeout", required_argument, 0, 't'},
        {"profiling", no_argument, 0, 0x106},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    char *config_file = NULL;

    while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
        case 'C':  // config file
            config_file = optarg;
            break;

        case 's':  // scenario
            if (strcmp(optarg, "detection") == 0) {
                config->scenario = RKNS_LAUNCH_SCENARIO_DETECTION;
            } else if (strcmp(optarg, "face") == 0) {
                config->scenario = RKNS_LAUNCH_SCENARIO_FACE;
            } else if (strcmp(optarg, "segment") == 0) {
                config->scenario = RKNS_LAUNCH_SCENARIO_SEGMENT;
            } else if (strcmp(optarg, "track") == 0) {
                config->scenario = RKNS_LAUNCH_SCENARIO_TRACK;
            } else if (strcmp(optarg, "custom") == 0) {
                config->scenario = RKNS_LAUNCH_SCENARIO_CUSTOM;
            } else {
                fprintf(stderr, "Error: Invalid scenario type: %s\n", optarg);
                return RKNS_FAILURE;
            }
            break;

        case 'i':  // input type
            if (strcmp(optarg, "ROCKIT") == 0) {
                config->inputType = RKNS_INPUT_SOURCE_ROCKIT;
            } else if (strcmp(optarg, "GSTREAMER") == 0) {
                config->inputType = RKNS_INPUT_SOURCE_GSTREAMER;
            } else if (strcmp(optarg, "OPENCV") == 0) {
                config->inputType = RKNS_INPUT_SOURCE_OPENCV;
            } else if (strcmp(optarg, "CUSTOM") == 0) {
                config->inputType = RKNS_INPUT_SOURCE_CUSTOM;
            } else {
                fprintf(stderr, "Error: Invalid input type: %s\n", optarg);
                return RKNS_FAILURE;
            }
            break;

        case 0x100:  // input-path
            rkos_strncpy(config->inputPath, optarg, MAX_PATH - 1);
            break;

        case 'W':  // width
            config->width = atoi(optarg);
            break;

        case 'H':  // height
            config->height = atoi(optarg);
            break;

        case 'F':  // fps
            config->frameRate = atoi(optarg);
            break;

        case 'm':  // model type
            if (strcmp(optarg, "ALM") == 0) {
                config->modelType = RKNS_MODEL_TYPE_ALM;
            } else if (strcmp(optarg, "CLASSIFY") == 0) {
                config->modelType = RKNS_MODEL_TYPE_CLASSIFY;
            } else if (strcmp(optarg, "DETECT") == 0) {
                config->modelType = RKNS_MODEL_TYPE_DETECT;
            } else if (strcmp(optarg, "LLM") == 0) {
                config->modelType = RKNS_MODEL_TYPE_LLM;
            } else if (strcmp(optarg, "SEGMENT") == 0) {
                config->modelType = RKNS_MODEL_TYPE_SEGMENT;
            } else if (strcmp(optarg, "TRACK") == 0) {
                config->modelType = RKNS_MODEL_TYPE_TRACK;
            } else if (strcmp(optarg, "TTA") == 0) {
                config->modelType = RKNS_MODEL_TYPE_TTA;
            } else if (strcmp(optarg, "VLM") == 0) {
                config->modelType = RKNS_MODEL_TYPE_VLM;
            } else {
                fprintf(stderr, "Error: Invalid model type: %s\n", optarg);
                return RKNS_FAILURE;
            }
            break;

        case 0x101:  // model-path
            rkos_strncpy(config->modelPath, optarg, MAX_MODEL_PATH - 1);
            break;

        case 'c':  // confidence
            config->confidenceThreshold = atof(optarg);
            break;

        case 0x102:  // enable-overlay
            config->enableOverlay = RKNS_TRUE;
            break;

        case 0x103:  // disable-overlay
            config->enableOverlay = RKNS_FALSE;
            break;

        case 'o':  // output type
            if (strcmp(optarg, "APP") == 0) {
                config->outputType = RKNS_OUTPUT_TYPE_APP;
            } else if (strcmp(optarg, "ROCKIT") == 0) {
                config->outputType = RKNS_OUTPUT_TYPE_ROCKIT;
            } else if (strcmp(optarg, "GSTREAM") == 0) {
                config->outputType = RKNS_OUTPUT_TYPE_GSTREAM;
            } else if (strcmp(optarg, "OPENCV") == 0) {
                config->outputType = RKNS_OUTPUT_TYPE_OPENCV;
            } else {
                fprintf(stderr, "Error: Invalid output type: %s\n", optarg);
                return RKNS_FAILURE;
            }
            break;

        case 0x104:  // output-path
            rkos_strncpy(config->outputPath, optarg, MAX_PATH - 1);
            break;

        case 0x105:  // watch-depth
            config->watchDepth = atoi(optarg);
            break;

        case 't':  // timeout
            config->timeout = atoi(optarg);
            break;

        case 0x106:  // profiling
            config->enableProfiling = RKNS_TRUE;
            break;

        case 'v':  // verbose
            config->verbose = RKNS_TRUE;
            break;

        case 'h':  // help
            print_usage(argv[0]);
            exit(0);

        default:
            print_usage(argv[0]);
            return RKNS_FAILURE;
        }
    }

    // 如果指定了配置文件，从 YAML 加载配置
    if (config_file) {
        printf("[INFO] Loading configuration from: %s\n", config_file);
        ret = rkns_launch_load_config_from_yaml(config, config_file);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load configuration from: %s\n", config_file);
            return RKNS_FAILURE;
        }
    } else {
        printf("[INFO] No config file specified, using CLI mode\n");
    }

    return RKNS_SUCCESS;
}

/* ============================================================
 * 流水线管理函数（模块注册表驱动）
 * ============================================================ */

/**
 * @brief 流水线上下文结构体
 */
typedef struct _RKNSLaunchPipeline {
    RKNSLaunchModuleState moduleStates[MAX_MODULES];
    RKNS_U32 moduleCount;
    RKNS_BOOL bindingsBound[MAX_BINDINGS];
} RKNSLaunchPipeline;

#define MAX_PIPELINES_RUNNING  4

/**
 * @brief 流水线上下文（支持多个 pipeline 同时运行）
 */
typedef struct _RKNSLaunchPipelineContext {
    const RKNSLaunchPipelineDesc *desc;
    RKNSLaunchPipeline state;
    RKNS_BOOL created;
    RKNS_BOOL started;
} RKNSLaunchPipelineContext;

/* ============================================================
 * 模块操作函数实现
 * ============================================================ */

/* ---------- INPUT 模块 ---------- */

RKNS_S32 input_parse_config(YamlDocument *doc, const char *base_path,
                                    RKNSLaunchModule *module) {
    RKNSInputParam *param = MODULE_CONFIG(module, RKNSInputParam);
    char path[256];
    const char *str;

    rkos_memset(param, 0, sizeof(RKNSInputParam));

    /* INPUT 模块无设备参数，所有参数在 channels 下配置 */
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.type", base_path);
    str = yaml_get_string(doc, path, "CUSTOM");
    param->sourceType = parse_input_type(str);

    /* 检测 op_type 字段以确定是否为音频输入 */
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.op_type", base_path);
    str = yaml_get_string(doc, path, "");
    if (strcmp(str, "AI_CAPTURE") == 0) {
        /* 音频输入模式 - Rockit 源 */
        param->sourceType = RKNS_INPUT_SOURCE_ROCKIT;

        /* 解析 source 对象 */
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.source.dev_id", base_path);
        param->rockit.devId = yaml_get_int(doc, path, 0);

        snprintf(path, sizeof(path), "%s.channels.0.chn_config.source.chn_id", base_path);
        param->rockit.chnId = yaml_get_int(doc, path, 0);

        /* 设置模块 ID 为 AI (RK_ID_AI = 10) */
        param->rockit.modId = 10;
    } else {
        /* 视频输入模式（默认） - 使用自定义源 */
        param->sourceType = RKNS_INPUT_SOURCE_CUSTOM;
    }

    /* 解析 depth 参数 */
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.depth", base_path);
    param->watcherDepth = (RKNS_U32)yaml_get_int(doc, path, 1);

    /* 获取通道 ID */
    snprintf(path, sizeof(path), "%s.channels.0.chn_id", base_path);
    module->chnId = (RKNS_S32)yaml_get_int(doc, path, 0);

    /* 打印 INPUT 模块配置参数 */
    printf("[INFO] INPUT Module Config:\n");
    printf("[INFO]   chn_id: %d\n", module->chnId);
    printf("[INFO]   source_type: %d (%s)\n", param->sourceType,
           param->sourceType == RKNS_INPUT_SOURCE_ROCKIT ? "ROCKIT" : "CUSTOM");
    if (param->sourceType == RKNS_INPUT_SOURCE_ROCKIT) {
        printf("[INFO]   op_type: AI_CAPTURE\n");
        printf("[INFO]   source.module: AI (modId=%d)\n", param->rockit.modId);
        printf("[INFO]   source.dev_id: %d\n", param->rockit.devId);
        printf("[INFO]   source.chn_id: %d\n", param->rockit.chnId);
    }
    printf("[INFO]   depth: %u\n", param->watcherDepth);

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 input_create(const RKNSLaunchModule *module,
                              RKNSLaunchModuleState *state) {
    RKNSInputParam *param = MODULE_CONFIG(module, RKNSInputParam);
    RKNS_S32 ret;

    ret = RKNS_INPUT_CreateChn(module->devId, module->chnId, param);
    if (ret != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] RKNS_INPUT_CreateChn failed: %d\n", ret);
        return ret;
    }
    state->chn.enModId = RKNS_ID_INPUT;
    state->chn.s32DevId = module->devId;
    state->chn.s32ChnId = module->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 input_destroy(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    if (state->created) {
        RKNS_INPUT_DestroyChn(module->devId, module->chnId);
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 input_start(const RKNSLaunchModule *module,
                             RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_INPUT_StartChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_TRUE;
    return ret;
}

RKNS_S32 input_stop(const RKNSLaunchModule *module,
                            RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_INPUT_StopChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_FALSE;
    return ret;
}

/* ---------- INFER 模块 ---------- */

/**
 * 字符串池辅助：从 YAML 读取一个字符串字段，复制到 str_pool，并让 *pptr 指向它。
 * 成功返回 RKNS_SUCCESS，字符串为空或空间不足时 *pptr 保持 NULL。
 */
static RKNS_S32 str_pool_alloc(YamlDocument *doc, const char *yaml_path,
                               char *str_pool, size_t pool_size, size_t *offset,
                               RKNS_CHAR **pptr) {
    const char *val = yaml_get_string(doc, yaml_path, "");
    if (val[0] == '\0') {
        return RKNS_SUCCESS;  /* 空值不分配，*pptr 保持 NULL */
    }
    size_t len = strlen(val) + 1;
    if (*offset + len >= pool_size) {
        return RKNS_FAILURE;  /* 空间不足 */
    }
    rkos_strncpy(str_pool + *offset, val, pool_size - *offset - 1);
    str_pool[*offset + pool_size - *offset - 1] = '\0';
    *pptr = str_pool + *offset;
    *offset += len;
    return RKNS_SUCCESS;
}

/* ---- 模型类型特有解析函数 ---- */

/**
 * @brief ALM 模型属性解析（Whisper ASR 等）
 * YAML 字段：alm_task, alm_encoder_model, alm_decoder_model,
 *            alm_mel_filters, alm_vocab
 */
static RKNS_S32 alm_parse_model_config(YamlDocument *doc, const char *base_path,
                                       RKNS_INFER_ATTR_S *attr,
                                       char *str_pool, size_t pool_size,
                                       size_t *offset) {
    RKNS_ALM_MODEL_ATTR_S *m = &attr->stAlmAttr;
    char path[256];

    snprintf(path, sizeof(path), "%s.alm_task", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pTask);

    snprintf(path, sizeof(path), "%s.alm_encoder_model", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pEncoderModelName);

    snprintf(path, sizeof(path), "%s.alm_decoder_model", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pDecoderModelName);

    snprintf(path, sizeof(path), "%s.alm_mel_filters", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pMelFiltersName);

    snprintf(path, sizeof(path), "%s.alm_vocab", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pVocabName);

    /* 解析缓存时间（frame_time），单位为毫秒，默认1000ms */
    snprintf(path, sizeof(path), "%s.frame_time", base_path);
    m->u32CacheTime = (RKNS_U32)yaml_get_int(doc, path, 1000);

    return RKNS_SUCCESS;
}

/* DETECT/CLASSIFY/LLM/SEGMENT/TRACK/TTA/VLM 当前为预留空字段，无需特有解析 */

/**
 * @brief 模型类型解析注册表
 *
 * 每种模型类型对应一个解析函数，负责填充 RKNS_INFER_ATTR_S 联合体中
 * 该类型特有的成员。新增模型类型只需：
 *   1. 实现 parse 函数（签名同 ModelConfigParseFunc）
 *   2. 在此表中添加一行
 */
typedef RKNS_S32 (*ModelConfigParseFunc)(YamlDocument *doc, const char *base_path,
                                         RKNS_INFER_ATTR_S *attr,
                                         char *str_pool, size_t pool_size,
                                         size_t *offset);

static const struct {
    RKNS_MODEL_TYPE_E type;
    ModelConfigParseFunc parse;
} g_model_config_parsers[] = {
    { RKNS_MODEL_TYPE_ALM,      alm_parse_model_config },
    { RKNS_MODEL_TYPE_CLASSIFY, NULL },  /* 预留 */
    { RKNS_MODEL_TYPE_DETECT,   NULL },  /* 预留 */
    { RKNS_MODEL_TYPE_LLM,      NULL },  /* 预留 */
    { RKNS_MODEL_TYPE_SEGMENT,  NULL },  /* 预留 */
    { RKNS_MODEL_TYPE_TRACK,    NULL },  /* 预留 */
    { RKNS_MODEL_TYPE_TTA,      NULL },  /* 预留 */
    { RKNS_MODEL_TYPE_VLM,      NULL },  /* 预留 */
};
#define MODEL_CONFIG_PARSERS_COUNT \
    (sizeof(g_model_config_parsers) / sizeof(g_model_config_parsers[0]))

static ModelConfigParseFunc find_model_config_parser(RKNS_MODEL_TYPE_E type) {
    RKNS_U32 i;
    for (i = 0; i < MODEL_CONFIG_PARSERS_COUNT; i++) {
        if (g_model_config_parsers[i].type == type) {
            return g_model_config_parsers[i].parse;
        }
    }
    return NULL;
}

RKNS_S32 infer_parse_config(YamlDocument *doc, const char *base_path,
                                    RKNSLaunchModule *module) {
    RKNS_INFER_ATTR_S *attr = MODULE_CONFIG(module, RKNS_INFER_ATTR_S);
    char path[256];
    const char *str;

    rkos_memset(attr, 0, sizeof(RKNS_INFER_ATTR_S));

    /* INFER 模块无设备参数，所有参数在 channels 下配置 */
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.model_type", base_path);
    str = yaml_get_string(doc, path, "DETECT");
    attr->enModelType = parse_model_type(str);

    snprintf(path, sizeof(path), "%s.channels.0.chn_config.input_queue_count", base_path);
    attr->u32InputQueueCnt = (RKNS_U32)yaml_get_int(doc, path, 3);

    snprintf(path, sizeof(path), "%s.channels.0.chn_config.output_buffer_count", base_path);
    attr->u32BufferCnt = (RKNS_U32)yaml_get_int(doc, path, 3);

    snprintf(path, sizeof(path), "%s.channels.0.chn_config.depth", base_path);
    attr->depth = (RKNS_U32)yaml_get_int(doc, path, 0);

    snprintf(path, sizeof(path), "%s.channels.0.chn_config.src_frame_rate", base_path);
    attr->s32SrcFrameRate = (RKNS_S32)yaml_get_int(doc, path, 0);

    snprintf(path, sizeof(path), "%s.channels.0.chn_config.dst_frame_rate", base_path);
    attr->s32DstFrameRate = (RKNS_S32)yaml_get_int(doc, path, 0);

    /* 字符串池：stModelAttr 和模型特有属性的 char* 指针都指向此区域 */
    {
        size_t attr_size = sizeof(RKNS_INFER_ATTR_S);
        char *str_pool = (char *)module->configBuf + attr_size;
        size_t pool_remain = RKNS_MODULE_CONFIG_SIZE - attr_size;
        size_t offset = 0;

        /* 公共属性：stModelAttr */
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.module_name", base_path);
        str_pool_alloc(doc, path, str_pool, pool_remain, &offset,
                       &attr->stModelAttr.pModuleName);

        snprintf(path, sizeof(path), "%s.channels.0.chn_config.model_path", base_path);
        str_pool_alloc(doc, path, str_pool, pool_remain, &offset,
                       &attr->stModelAttr.pDataPath);

        snprintf(path, sizeof(path), "%s.channels.0.chn_config.model_auth_config", base_path);
        str_pool_alloc(doc, path, str_pool, pool_remain, &offset,
                       &attr->stModelAttr.pAuthConfig);

        /* 模型特有属性：通过注册表按 enModelType 查找对应解析函数 */
        {
            ModelConfigParseFunc model_parse = find_model_config_parser(attr->enModelType);
            if (model_parse) {
                /* 使用 channels.0.chn_config 作为基础路径 */
                char chn_config_path[256];
                snprintf(chn_config_path, sizeof(chn_config_path), "%s.channels.0.chn_config", base_path);
                model_parse(doc, chn_config_path, attr, str_pool, pool_remain, &offset);
            }
        }
    }

    /* 获取通道 ID */
    snprintf(path, sizeof(path), "%s.channels.0.chn_id", base_path);
    module->chnId = (RKNS_S32)yaml_get_int(doc, path, 0);

    /* 打印 INFER 模块配置参数 */
    printf("[INFO] INFER Module Config:\n");
    printf("[INFO]   chn_id: %d\n", module->chnId);
    printf("[INFO]   model_type: %d\n", attr->enModelType);
    printf("[INFO]   module_name: %s\n", attr->stModelAttr.pModuleName ? attr->stModelAttr.pModuleName : "(null)");
    printf("[INFO]   model_path: %s\n", attr->stModelAttr.pDataPath ? attr->stModelAttr.pDataPath : "(null)");
    printf("[INFO]   input_queue_count: %u\n", attr->u32InputQueueCnt);
    printf("[INFO]   output_buffer_count: %u\n", attr->u32BufferCnt);
    printf("[INFO]   depth: %u\n", attr->depth);
    printf("[INFO]   src_frame_rate: %d\n", attr->s32SrcFrameRate);
    printf("[INFO]   dst_frame_rate: %d\n", attr->s32DstFrameRate);
    if (attr->enModelType == RKNS_MODEL_TYPE_ALM) {
        printf("[INFO]   ALM Model Attr:\n");
        printf("[INFO]     task: %s\n", attr->stAlmAttr.pTask ? attr->stAlmAttr.pTask : "(null)");
        printf("[INFO]     encoder_model: %s\n", attr->stAlmAttr.pEncoderModelName ? attr->stAlmAttr.pEncoderModelName : "(null)");
        printf("[INFO]     decoder_model: %s\n", attr->stAlmAttr.pDecoderModelName ? attr->stAlmAttr.pDecoderModelName : "(null)");
        printf("[INFO]     mel_filters: %s\n", attr->stAlmAttr.pMelFiltersName ? attr->stAlmAttr.pMelFiltersName : "(null)");
        printf("[INFO]     vocab: %s\n", attr->stAlmAttr.pVocabName ? attr->stAlmAttr.pVocabName : "(null)");
        printf("[INFO]     cache_time: %u ms\n", attr->stAlmAttr.u32CacheTime);
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 infer_create(const RKNSLaunchModule *module,
                              RKNSLaunchModuleState *state) {
    const RKNS_INFER_ATTR_S *attr = MODULE_CONFIG(module, RKNS_INFER_ATTR_S);
    RKNS_S32 ret;

    /* INFER API 只接收 chnId，不接收 devId */
    ret = RKNS_INFER_CreateChn((INFER_CHN)module->chnId, attr);
    if (ret != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] RKNS_INFER_CreateChn failed: %d\n", ret);
        return ret;
    }
    state->chn.enModId = RKNS_ID_INFER;
    state->chn.s32DevId = module->devId;
    state->chn.s32ChnId = module->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 infer_destroy(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    if (state->created) {
        RKNS_INFER_DestroyChn((INFER_CHN)module->chnId);
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 infer_start(const RKNSLaunchModule *module,
                             RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_INFER_StartChn((INFER_CHN)module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_TRUE;
    return ret;
}

RKNS_S32 infer_stop(const RKNSLaunchModule *module,
                            RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_INFER_StopChn((INFER_CHN)module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_FALSE;
    return ret;
}

/* ---------- GRAPHICS 模块 ---------- */

RKNS_S32 graphics_parse_config(YamlDocument *doc, const char *base_path,
                                       RKNSLaunchModule *module) {
    RKNSGraphicsParam *param = MODULE_CONFIG(module, RKNSGraphicsParam);
    RKNS_OVERLAY_RULE_S *rule = MODULE_SECONDARY(module, RKNS_OVERLAY_RULE_S);
    char path[256];
    const char *str;
    int rule_count;

    rkos_memset(param, 0, sizeof(RKNSGraphicsParam));
    rkos_memset(rule, 0, sizeof(RKNS_OVERLAY_RULE_S));

    /* 主配置: RKNSGraphicsParam */
    param->type = RKNS_GRAPHICS_TYPE_OVERLAY;

    snprintf(path, sizeof(path), "%s.width", base_path);
    param->attr.width = (RKNS_U32)yaml_get_int(doc, path, 640);

    snprintf(path, sizeof(path), "%s.height", base_path);
    param->attr.height = (RKNS_U32)yaml_get_int(doc, path, 480);

    param->attr.stride = param->attr.width;

    snprintf(path, sizeof(path), "%s.pixel_format", base_path);
    str = yaml_get_string(doc, path, "NV12");
    param->attr.pixelFmt = parse_pixel_format(str);

    param->attr.bgColor.a = 0xFF;
    param->attr.maxBoxPerFrame = 32;
    param->attr.lineWidth = 2;
    param->attr.textScale = 1;

    /* 次级配置: RKNS_OVERLAY_RULE_S */
    snprintf(path, sizeof(path), "%s.enable_box", base_path);
    rule->enableBox = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

    snprintf(path, sizeof(path), "%s.enable_text", base_path);
    rule->enableText = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

    snprintf(path, sizeof(path), "%s.global_score_threshold", base_path);
    rule->globalScoreThreshold = (RKNS_FLOAT)yaml_get_float(doc, path, 0.4);

    /* 解析 overlay rules 子列表 */
    snprintf(path, sizeof(path), "%s.rules", base_path);
    rule_count = yaml_get_count(doc, path);
    if (rule_count > 0 && rule_count <= RKNS_OVERLAY_MAX_CLS_NUM) {
        int ri;
        rule->clsNum = (RKNS_U32)rule_count;
        for (ri = 0; ri < rule_count; ri++) {
            RKNS_OVERLAY_RULE_ITEM_S *item = &rule->clsRule[ri];

            snprintf(path, sizeof(path), "%s.rules.%d.class_name", base_path, ri);
            str = yaml_get_string(doc, path, "unknown");
            rkos_strncpy(item->clsName, str, RKNS_OVERLAY_MAX_NAME_LEN - 1);

            snprintf(path, sizeof(path), "%s.rules.%d.enable", base_path, ri);
            item->enable = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

            snprintf(path, sizeof(path), "%s.rules.%d.score_threshold", base_path, ri);
            item->scoreThreshold = (RKNS_FLOAT)yaml_get_float(doc, path, 0.5);

            snprintf(path, sizeof(path), "%s.rules.%d.color.r", base_path, ri);
            item->color.r = (RKNS_U8)yaml_get_int(doc, path, 255);

            snprintf(path, sizeof(path), "%s.rules.%d.color.g", base_path, ri);
            item->color.g = (RKNS_U8)yaml_get_int(doc, path, 0);

            snprintf(path, sizeof(path), "%s.rules.%d.color.b", base_path, ri);
            item->color.b = (RKNS_U8)yaml_get_int(doc, path, 0);

            snprintf(path, sizeof(path), "%s.rules.%d.color.a", base_path, ri);
            item->color.a = (RKNS_U8)yaml_get_int(doc, path, 255);
        }
    }

    /* 打印 GRAPHICS 模块配置参数 */
    printf("[INFO] GRAPHICS Module Config:\n");
    printf("[INFO]   chn_id: %d\n", module->chnId);
    printf("[INFO]   graphics_type: OVERLAY\n");
    printf("[INFO]   width: %u\n", param->attr.width);
    printf("[INFO]   height: %u\n", param->attr.height);
    printf("[INFO]   pixel_format: %s\n",
           param->attr.pixelFmt == RKNS_OVERLAY_PIXFMT_NV12 ? "NV12" :
           param->attr.pixelFmt == RKNS_OVERLAY_PIXFMT_RGB888 ? "RGB888" : "ARGB8888");
    printf("[INFO]   max_box_per_frame: %u\n", param->attr.maxBoxPerFrame);
    printf("[INFO]   line_width: %u\n", param->attr.lineWidth);
    printf("[INFO]   text_scale: %u\n", param->attr.textScale);
    printf("[INFO]   enable_box: %s\n", rule->enableBox ? "true" : "false");
    printf("[INFO]   enable_text: %s\n", rule->enableText ? "true" : "false");
    printf("[INFO]   global_score_threshold: %.2f\n", rule->globalScoreThreshold);
    printf("[INFO]   rules_count: %u\n", rule->clsNum);
    if (rule->clsNum > 0) {
        RKNS_U32 ri;
        for (ri = 0; ri < rule->clsNum; ri++) {
            const RKNS_OVERLAY_RULE_ITEM_S *item = &rule->clsRule[ri];
            printf("[INFO]   rules[%u]:\n", ri);
            printf("[INFO]     class_name: %s\n", item->clsName);
            printf("[INFO]     enable: %s\n", item->enable ? "true" : "false");
            printf("[INFO]     score_threshold: %.2f\n", item->scoreThreshold);
            printf("[INFO]     color: A=%d, R=%d, G=%d, B=%d\n",
                   item->color.a, item->color.r, item->color.g, item->color.b);
        }
    }

    module->configParsed = RKNS_TRUE;
    module->hasSecondaryConfig = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 graphics_create(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    const RKNSGraphicsParam *param = MODULE_CONFIG(module, RKNSGraphicsParam);
    const RKNS_OVERLAY_RULE_S *rule = MODULE_SECONDARY(module, RKNS_OVERLAY_RULE_S);
    RKNS_S32 ret;

    ret = RKNS_GRAPHICS_CreateChn(module->devId, module->chnId,
                                   (RKNSGraphicsParam *)param);
    if (ret != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] RKNS_GRAPHICS_CreateChn failed: %d\n", ret);
        return ret;
    }

    if (module->hasSecondaryConfig) {
        ret = RKNS_GRAPHICS_SetConfig(module->devId, module->chnId, rule);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RKNS_GRAPHICS_SetConfig failed: %d\n", ret);
            RKNS_GRAPHICS_DestroyChn(module->devId, module->chnId);
            return ret;
        }
    }

    state->chn.enModId = RKNS_ID_GRAPHICS;
    state->chn.s32DevId = module->devId;
    state->chn.s32ChnId = module->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 graphics_destroy(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    if (state->created) {
        RKNS_GRAPHICS_DestroyChn(module->devId, module->chnId);
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 graphics_start(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_GRAPHICS_StartChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_TRUE;
    return ret;
}

RKNS_S32 graphics_stop(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_GRAPHICS_StopChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_FALSE;
    return ret;
}

/* ---------- OUTPUT 模块 ---------- */

RKNS_S32 output_parse_config(YamlDocument *doc, const char *base_path,
                                      RKNSLaunchModule *module) {
    RKNSOutputParam *param = MODULE_CONFIG(module, RKNSOutputParam);
    char path[256];
    const char *str;

    rkos_memset(param, 0, sizeof(RKNSOutputParam));

    snprintf(path, sizeof(path), "%s.type", base_path);
    str = yaml_get_string(doc, path, "APP");
    param->type = parse_output_type(str);

    snprintf(path, sizeof(path), "%s.watch_depth", base_path);
    param->watchDepth = (int)yaml_get_int(doc, path, 1);

    /* 操作类型（APP 类型时有效） */
    snprintf(path, sizeof(path), "%s.op_type", base_path);
    str = yaml_get_string(doc, path, "PRINT");
    if (param->type == RKNS_OUTPUT_TYPE_APP) {
        if (strcmp(str, "SAVE_FILE") == 0) {
            param->app.cmd = RKNS_APP_CMD_SAVE_FILE;
        } else {
            param->app.cmd = RKNS_APP_CMD_PRINT;
        }
    }

    /* 打印 OUTPUT 模块配置参数 */
    printf("[INFO] OUTPUT Module Config:\n");
    printf("[INFO]   chn_id: %d\n", module->chnId);
    printf("[INFO]   output_type: %s\n",
           param->type == RKNS_OUTPUT_TYPE_APP ? "APP" :
           param->type == RKNS_OUTPUT_TYPE_ROCKIT ? "ROCKIT" :
           param->type == RKNS_OUTPUT_TYPE_GSTREAM ? "GSTREAM" : "OPENCV");
    if (param->type == RKNS_OUTPUT_TYPE_APP) {
        printf("[INFO]   op_type: %s\n",
               param->app.cmd == RKNS_APP_CMD_PRINT ? "PRINT" : "SAVE_FILE");
    }
    printf("[INFO]   watch_depth: %d\n", param->watchDepth);

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 output_create(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    const RKNSOutputParam *param = MODULE_CONFIG(module, RKNSOutputParam);
    RKNSOutputParam createParam;
    RKNS_S32 ret;

    /* 复制一份可修改的 param */
    rkos_memset(&createParam, 0, sizeof(createParam));
    createParam.type = param->type;
    createParam.watchDepth = param->watchDepth;

    switch (createParam.type) {
    case RKNS_OUTPUT_TYPE_APP:
        // 使用解析出来的操作类型
        createParam.app.cmd = param->app.cmd;
        break;
    case RKNS_OUTPUT_TYPE_ROCKIT:
        createParam.rockit.cmd = RKNS_ROCKIT_CMD_SEND_VO;
        createParam.rockit.sendVo.volayerId = 0;
        createParam.rockit.sendVo.voChnId = 0;
        break;
    case RKNS_OUTPUT_TYPE_GSTREAM:
        createParam.gstream.pipeline = "autovideosink";
        createParam.gstream.timeout = 5000;
        break;
    case RKNS_OUTPUT_TYPE_OPENCV:
        createParam.opencv.windowName = "RKNNStream Launch";
        createParam.opencv.width = 640;
        createParam.opencv.height = 480;
        break;
    default:
        fprintf(stderr, "[ERROR] Unsupported output type: %d\n", createParam.type);
        return RKNS_FAILURE;
    }

    ret = RKNS_OUTPUT_CreateChn(module->devId, module->chnId, &createParam);
    if (ret != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] RKNS_OUTPUT_CreateChn failed: %d\n", ret);
        return ret;
    }
    state->chn.enModId = RKNS_ID_OUTPUT;
    state->chn.s32DevId = module->devId;
    state->chn.s32ChnId = module->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 output_destroy(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    if (state->created) {
        RKNS_OUTPUT_DestroyChn(module->devId, module->chnId);
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 output_start(const RKNSLaunchModule *module,
                              RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_OUTPUT_StartChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_TRUE;
    return ret;
}

RKNS_S32 output_stop(const RKNSLaunchModule *module,
                             RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_OUTPUT_StopChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_FALSE;
    return ret;
}

/* ---------- Rockit VI 模块 ---------- */

RKNS_S32 rockit_vi_parse_config(YamlDocument *doc, const char *base_path,
                                        RKNSLaunchModule *module) {
    RockitVIConfig *cfg = MODULE_CONFIG(module, RockitVIConfig);
    char path[256];

    rkos_memset(cfg, 0, sizeof(RockitVIConfig));
    cfg->devId = module->devId;
    cfg->pipeId = module->pipeId;
    cfg->chnId = module->chnId;

    snprintf(path, sizeof(path), "%s.width", base_path);
    cfg->width = (RKNS_U32)yaml_get_int(doc, path, 1920);

    snprintf(path, sizeof(path), "%s.height", base_path);
    cfg->height = (RKNS_U32)yaml_get_int(doc, path, 1080);

    snprintf(path, sizeof(path), "%s.buffer_count", base_path);
    cfg->bufferCount = (RKNS_U32)yaml_get_int(doc, path, 2);

    snprintf(path, sizeof(path), "%s.depth", base_path);
    cfg->depth = (RKNS_U32)yaml_get_int(doc, path, 0);

    /* 打印 VI 模块配置参数 */
    printf("[INFO] VI Module Config:\n");
    printf("[INFO]   dev_id: %d\n", cfg->devId);
    printf("[INFO]   pipe_id: %d\n", cfg->pipeId);
    printf("[INFO]   chn_id: %d\n", cfg->chnId);
    printf("[INFO]   width: %u\n", cfg->width);
    printf("[INFO]   height: %u\n", cfg->height);
    printf("[INFO]   buffer_count: %u\n", cfg->bufferCount);
    printf("[INFO]   depth: %u\n", cfg->depth);

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vi_create(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    const RockitVIConfig *cfg = MODULE_CONFIG(module, RockitVIConfig);
    RK_S32 ret;

    /* 1. 配置 VI 设备 */
    {
        VI_DEV_ATTR_S stDevAttr;
        rkos_memset(&stDevAttr, 0, sizeof(stDevAttr));
        ret = RK_MPI_VI_GetDevAttr(cfg->devId, &stDevAttr);
        if (ret == -1) { /* RK_ERR_VI_NOT_CONFIG */
            ret = RK_MPI_VI_SetDevAttr(cfg->devId, &stDevAttr);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RK_MPI_VI_SetDevAttr failed: %#x\n", ret);
                return RKNS_FAILURE;
            }
        }
    }

    /* 2. 使能 VI 设备 */
    ret = RK_MPI_VI_GetDevIsEnable(cfg->devId);
    if (ret != 0) {
        ret = RK_MPI_VI_EnableDev(cfg->devId);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VI_EnableDev failed: %#x\n", ret);
            return RKNS_FAILURE;
        }
        state->viDevEnabled = RKNS_TRUE;
    }

    /* 3. 绑定设备到 pipe */
    {
        VI_DEV_BIND_PIPE_S stBindPipe;
        rkos_memset(&stBindPipe, 0, sizeof(stBindPipe));
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = cfg->pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(cfg->devId, &stBindPipe);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VI_SetDevBindPipe failed: %#x\n", ret);
            return RKNS_FAILURE;
        }
    }

    /* 4. 配置并使能 VI 通道 */
    {
        VI_CHN_ATTR_S stChnAttr;
        rkos_memset(&stChnAttr, 0, sizeof(stChnAttr));
        stChnAttr.stSize.u32Width = cfg->width;
        stChnAttr.stSize.u32Height = cfg->height;
        stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
        stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
        stChnAttr.u32Depth = cfg->depth;
        stChnAttr.stIspOpt.u32BufCount = cfg->bufferCount;
        stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;

        ret = RK_MPI_VI_SetChnAttr(cfg->pipeId, cfg->chnId, &stChnAttr);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VI_SetChnAttr failed: %#x\n", ret);
            return RKNS_FAILURE;
        }

        ret = RK_MPI_VI_EnableChn(cfg->pipeId, cfg->chnId);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VI_EnableChn failed: %#x\n", ret);
            return RKNS_FAILURE;
        }
        state->viChnEnabled = RKNS_TRUE;
    }

    state->chn.s32DevId = cfg->devId;
    state->chn.s32ChnId = cfg->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vi_destroy(const RKNSLaunchModule *module,
                                   RKNSLaunchModuleState *state) {
    const RockitVIConfig *cfg = MODULE_CONFIG(module, RockitVIConfig);
    if (state->viChnEnabled) {
        RK_MPI_VI_DisableChn(cfg->pipeId, cfg->chnId);
        state->viChnEnabled = RKNS_FALSE;
    }
    if (state->viDevEnabled) {
        RK_MPI_VI_DisableDev(cfg->devId);
        state->viDevEnabled = RKNS_FALSE;
    }
    state->created = RKNS_FALSE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vi_start(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    (void)module;
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vi_stop(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    (void)module;
    state->started = RKNS_FALSE;
    return RKNS_SUCCESS;
}

/* ---------- Rockit VO 模块 ---------- */

RKNS_S32 rockit_vo_parse_config(YamlDocument *doc, const char *base_path,
                                        RKNSLaunchModule *module) {
    RockitVOConfig *cfg = MODULE_CONFIG(module, RockitVOConfig);
    char path[256];

    rkos_memset(cfg, 0, sizeof(RockitVOConfig));
    cfg->devId = module->devId;
    cfg->layerId = module->layerId;
    cfg->chnId = module->chnId;

    snprintf(path, sizeof(path), "%s.width", base_path);
    cfg->layerWidth = (RKNS_U32)yaml_get_int(doc, path, 1920);

    snprintf(path, sizeof(path), "%s.height", base_path);
    cfg->layerHeight = (RKNS_U32)yaml_get_int(doc, path, 1080);

    cfg->dispWidth = cfg->layerWidth;
    cfg->dispHeight = cfg->layerHeight;
    cfg->intfType = 2; /* VO_INTF_MIPI */

    /* 打印 VO 模块配置参数 */
    printf("[INFO] VO Module Config:\n");
    printf("[INFO]   dev_id: %d\n", cfg->devId);
    printf("[INFO]   layer_id: %d\n", cfg->layerId);
    printf("[INFO]   chn_id: %d\n", cfg->chnId);
    printf("[INFO]   layer_width: %u\n", cfg->layerWidth);
    printf("[INFO]   layer_height: %u\n", cfg->layerHeight);
    printf("[INFO]   disp_width: %u\n", cfg->dispWidth);
    printf("[INFO]   disp_height: %u\n", cfg->dispHeight);
    printf("[INFO]   intf_type: %d (MIPI)\n", cfg->intfType);

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vo_create(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    const RockitVOConfig *cfg = MODULE_CONFIG(module, RockitVOConfig);
    RK_S32 ret;

    /* 1. 绑定图层到设备 */
    ret = RK_MPI_VO_BindLayer(cfg->layerId, cfg->devId, 0 /* VO_LAYER_MODE_VIDEO */);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] RK_MPI_VO_BindLayer failed: %#x\n", ret);
        return RKNS_FAILURE;
    }
    state->voLayerBound = RKNS_TRUE;

    /* 2. 设置 VO 设备公共属性 */
    {
        VO_PUB_ATTR_S stPubAttr;
        rkos_memset(&stPubAttr, 0, sizeof(stPubAttr));
        stPubAttr.enIntfType = cfg->intfType;
        stPubAttr.enIntfSync = 0; /* VO_OUTPUT_DEFAULT */
        ret = RK_MPI_VO_SetPubAttr(cfg->devId, &stPubAttr);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VO_SetPubAttr failed: %#x\n", ret);
            return RKNS_FAILURE;
        }
    }

    /* 3. 使能 VO 设备 */
    ret = RK_MPI_VO_Enable(cfg->devId);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] RK_MPI_VO_Enable failed: %#x\n", ret);
        return RKNS_FAILURE;
    }
    state->voDevEnabled = RKNS_TRUE;

    /* 4. 设置图层属性 */
    {
        VO_VIDEO_LAYER_ATTR_S stLayerAttr;
        rkos_memset(&stLayerAttr, 0, sizeof(stLayerAttr));
        stLayerAttr.enPixFormat = RK_FMT_YUV420SP;
        stLayerAttr.stDispRect.s32X = 0;
        stLayerAttr.stDispRect.s32Y = 0;
        stLayerAttr.stDispRect.u32Width = cfg->dispWidth;
        stLayerAttr.stDispRect.u32Height = cfg->dispHeight;
        stLayerAttr.stImageSize.u32Width = cfg->layerWidth;
        stLayerAttr.stImageSize.u32Height = cfg->layerHeight;
        ret = RK_MPI_VO_SetLayerAttr(cfg->layerId, &stLayerAttr);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VO_SetLayerAttr failed: %#x\n", ret);
            return RKNS_FAILURE;
        }
    }

    /* 5. 使能图层 */
    ret = RK_MPI_VO_EnableLayer(cfg->layerId);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] RK_MPI_VO_EnableLayer failed: %#x\n", ret);
        return RKNS_FAILURE;
    }
    state->voLayerEnabled = RKNS_TRUE;

    /* 6. 设置通道属性并使能 */
    {
        VO_CHN_ATTR_S stChnAttr;
        rkos_memset(&stChnAttr, 0, sizeof(stChnAttr));
        stChnAttr.stRect.s32X = 0;
        stChnAttr.stRect.s32Y = 0;
        stChnAttr.stRect.u32Width = cfg->layerWidth;
        stChnAttr.stRect.u32Height = cfg->layerHeight;
        ret = RK_MPI_VO_SetChnAttr(cfg->layerId, cfg->chnId, &stChnAttr);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VO_SetChnAttr failed: %#x\n", ret);
            return RKNS_FAILURE;
        }

        ret = RK_MPI_VO_EnableChn(cfg->layerId, cfg->chnId);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VO_EnableChn failed: %#x\n", ret);
            return RKNS_FAILURE;
        }
    }

    state->chn.s32DevId = cfg->layerId; /* VO 绑定使用 layerId 作为 devId */
    state->chn.s32ChnId = cfg->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vo_destroy(const RKNSLaunchModule *module,
                                   RKNSLaunchModuleState *state) {
    const RockitVOConfig *cfg = MODULE_CONFIG(module, RockitVOConfig);

    RK_MPI_VO_DisableChn(cfg->layerId, cfg->chnId);
    if (state->voLayerEnabled) {
        RK_MPI_VO_DisableLayer(cfg->layerId);
        state->voLayerEnabled = RKNS_FALSE;
    }
    if (state->voDevEnabled) {
        RK_MPI_VO_Disable(cfg->devId);
        state->voDevEnabled = RKNS_FALSE;
    }
    if (state->voLayerBound) {
        RK_MPI_VO_UnBindLayer(cfg->layerId, cfg->devId);
        state->voLayerBound = RKNS_FALSE;
    }
    state->created = RKNS_FALSE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vo_start(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    (void)module;
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vo_stop(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    (void)module;
    state->started = RKNS_FALSE;
    return RKNS_SUCCESS;
}

/* ---------- Rockit AI 模块 ---------- */

RKNS_S32 rockit_ai_parse_config(YamlDocument *doc, const char *base_path,
                                        RKNSLaunchModule *module) {
    RockitAIConfig *cfg = MODULE_CONFIG(module, RockitAIConfig);
    char path[256];
    const char *str;
    int dev_count, chn_count;
    int di, ci;

    rkos_memset(cfg, 0, sizeof(RockitAIConfig));

    /* ========== 获取设备数量并分配内存 ========== */
    snprintf(path, sizeof(path), "%s.devices", base_path);
    dev_count = yaml_get_count(doc, path);
    if (dev_count <= 0) dev_count = 1;  // 至少一个设备

    cfg->devCount = (RKNS_U32)dev_count;
    cfg->devConfigs = (RockitAIDevConfig*)rkos_kmalloc(sizeof(RockitAIDevConfig) * dev_count);
    if (cfg->devConfigs == NULL) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for %d devices\n", dev_count);
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->devConfigs, 0, sizeof(RockitAIDevConfig) * dev_count);

    /* ========== 解析每个设备 ========== */
    for (di = 0; di < dev_count; di++) {
        RockitAIDevConfig *dev = &cfg->devConfigs[di];

        /* 设备ID */
        snprintf(path, sizeof(path), "%s.devices.%d.dev_id", base_path, di);
        dev->devId = (RKNS_S32)yaml_get_int(doc, path, di);

        /* 声卡参数 */
        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.source_type", base_path, di);
        str = yaml_get_string(doc, path, "hw:0,0");
        rkos_strncpy(dev->soundCard, str, sizeof(dev->soundCard) - 1);

        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.sample_rate", base_path, di);
        dev->sampleRate = (RKNS_U32)yaml_get_int(doc, path, 16000);

        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.channels", base_path, di);
        dev->channels = (RKNS_U32)yaml_get_int(doc, path, 2);

        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.bit_width", base_path, di);
        dev->bitWidth = (RKNS_U32)yaml_get_int(doc, path, 16);

        /* 通道映射使能开关 */
        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.channel_map_enable", base_path, di);
        dev->channelMapEnable = (RKNS_BOOL)yaml_get_bool(doc, path, RKNS_FALSE);

        /* ========== 获取该设备的通道数量并分配内存 ========== */
        snprintf(path, sizeof(path), "%s.devices.%d.channels", base_path, di);
        chn_count = yaml_get_count(doc, path);
        if (chn_count <= 0) chn_count = 1;  // 至少一个通道

        dev->chnCount = (RKNS_U32)chn_count;
        dev->chnConfigs = (RockitAIChnConfig*)rkos_kmalloc(sizeof(RockitAIChnConfig) * chn_count);
        if (dev->chnConfigs == NULL) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for %d channels\n", chn_count);
            /* 释放之前分配的设备内存 */
            for (int j = 0; j < di; j++) {
                if (cfg->devConfigs[j].chnConfigs) {
                    rkos_kfree(cfg->devConfigs[j].chnConfigs);
                }
            }
            rkos_kfree(cfg->devConfigs);
            cfg->devConfigs = NULL;
            cfg->devCount = 0;
            return RKNS_FAILURE;
        }
        rkos_memset(dev->chnConfigs, 0, sizeof(RockitAIChnConfig) * chn_count);

        /* ========== 解析该设备的所有通道 ========== */
        for (ci = 0; ci < chn_count; ci++) {
            RockitAIChnConfig *chn = &dev->chnConfigs[ci];

            /* 通道ID */
            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_id", base_path, di, ci);
            chn->chnId = (RKNS_S32)yaml_get_int(doc, path, ci);

            /* 通道音频参数 */
            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.sample_rate", base_path, di, ci);
            chn->sampleRate = (RKNS_U32)yaml_get_int(doc, path, dev->sampleRate);

            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.channels", base_path, di, ci);
            chn->channels = (RKNS_U32)yaml_get_int(doc, path, dev->channels);

            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.bit_width", base_path, di, ci);
            chn->bitWidth = (RKNS_U32)yaml_get_int(doc, path, dev->bitWidth);

            /* 通道处理参数 */
            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.frame_time", base_path, di, ci);
            chn->frameTime = (RKNS_U32)yaml_get_int(doc, path, 100);

            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.buffer_count", base_path, di, ci);
            chn->bufferCount = (RKNS_U32)yaml_get_int(doc, path, 4);

            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.depth", base_path, di, ci);
            chn->depth = (RKNS_U32)yaml_get_int(doc, path, 0);

            /* 声道映射配置 */
            chn->channelMap.chnCount = chn->channels;  // 默认通道声道数等于channels
            /* 解析 channel_map 数组，如 [0], [0,1], [1] 等 */
            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.channel_map", base_path, di, ci);
            int track_count = yaml_get_count(doc, path);
            if (track_count > 0) {
                /* 使用配置的映射 */
                if (track_count > MAX_CHN_CHNS) track_count = MAX_CHN_CHNS;
                chn->channelMap.chnCount = (RKNS_U32)track_count;
                for (int ti = 0; ti < track_count; ti++) {
                    char channel_path[512];
                    snprintf(channel_path, sizeof(channel_path), "%s.%d", path, ti);
                    chn->channelMap.chnMap[ti] = (RKNS_U32)yaml_get_int(doc, channel_path, ti);
                }
            } else {
                /* 默认映射：通道声道0->声卡声道0, 通道声道1->声卡声道1 */
                for (int ti = 0; ti < (int)chn->channelMap.chnCount && ti < MAX_CHN_CHNS; ti++) {
                    chn->channelMap.chnMap[ti] = (RKNS_U32)ti;
                }
            }

            /* 轨道模式配置 */
            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.track_mode", base_path, di, ci);
            chn->trackMode = (RKNS_S32)yaml_get_int(doc, path, 0);  // 默认 AUDIO_TRACK_NORMAL

            /* VQE 配置 */
            snprintf(path, sizeof(path), "%s.devices.%d.channels.%d.chn_config.vqe_config_path", base_path, di, ci);
            str = yaml_get_string(doc, path, "");
            rkos_strncpy(chn->vqeConfigPath, str, sizeof(chn->vqeConfigPath) - 1);
        }
    }

    /* 打印 AI 模块配置参数 */
    printf("[INFO] AI Module Config:\n");
    printf("[INFO]   Total devices: %u\n", cfg->devCount);
    for (di = 0; di < (int)cfg->devCount; di++) {
        RockitAIDevConfig *dev = &cfg->devConfigs[di];
        printf("[INFO]   Device[%d]:\n", di);
        printf("[INFO]     dev_id: %d\n", dev->devId);
        printf("[INFO]     source_type: %s\n", dev->soundCard);
        printf("[INFO]     sample_rate: %u Hz\n", dev->sampleRate);
        printf("[INFO]     channels: %u\n", dev->channels);
        printf("[INFO]     bit_width: %u bit\n", dev->bitWidth);
        printf("[INFO]     Channels (%u):\n", dev->chnCount);
        for (ci = 0; ci < (int)dev->chnCount; ci++) {
            RockitAIChnConfig *chn = &dev->chnConfigs[ci];
            printf("[INFO]       Channel[%d]: chn_id=%d, sampleRate=%u, channels=%u, bitWidth=%u\n",
                   ci, chn->chnId, chn->sampleRate, chn->channels, chn->bitWidth);
            printf("[INFO]         frame_time: %u ms, buffer_count: %u, depth: %u, track_mode: %d\n",
                   chn->frameTime, chn->bufferCount, chn->depth, chn->trackMode);
            if (chn->vqeConfigPath[0]) {
                printf("[INFO]         vqe_config_path: %s\n", chn->vqeConfigPath);
            }
            /* 只有当通道映射启用时才打印声道映射 */
            if (dev->channelMapEnable) {
                printf("[INFO]         Channel Map: chnTracks=%u -> devTracks=[",
                       chn->channelMap.chnCount);
                for (int ti = 0; ti < (int)chn->channelMap.chnCount && ti < MAX_CHN_CHNS; ti++) {
                    printf("%u%s", chn->channelMap.chnMap[ti],
                           (ti < (int)chn->channelMap.chnCount - 1 && ti < MAX_CHN_CHNS - 1) ? ", " : "");
                }
                printf("]\n");
            }
        }
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_ai_create(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    const RockitAIConfig *cfg = MODULE_CONFIG(module, RockitAIConfig);
    RK_S32 ret;
    AIO_ATTR_S aiAttr;
    AI_CHN_PARAM_S chnParam;

    int di, ci;

    if (cfg->devCount == 0 || cfg->devConfigs == NULL) {
        fprintf(stderr, "[ERROR] No device config available\n");
        return RKNS_FAILURE;
    }

    /* 初始化AI状态 */
    rkos_memset(&state->aiState, 0, sizeof(state->aiState));
    state->aiState.devCount = cfg->devCount;

    printf("[INFO]   Creating AI module with %u devices\n", cfg->devCount);

    /* ========== 遍历所有设备 ========== */
    for (di = 0; di < (int)cfg->devCount && di < MAX_AI_DEVICES; di++) {
        RockitAIDevConfig *dev = &cfg->devConfigs[di];

        if (dev->chnCount == 0 || dev->chnConfigs == NULL) {
            fprintf(stderr, "[WARNING] Device %d has no channels, skipping\n", di);
            continue;
        }

        printf("[INFO]   Creating AI device[%d]: devId=%d, soundCard=%s\n",
               di, dev->devId, dev->soundCard);
        printf("[INFO]     Device params: sampleRate=%u, channels=%u, bitWidth=%u\n",
               dev->sampleRate, dev->channels, dev->bitWidth);
        printf("[INFO]     Channel map enable: %s\n", dev->channelMapEnable ? "true" : "false");

        rkos_memset(&aiAttr, 0, sizeof(aiAttr));

        /* 配置 AI 设备属性 - 使用声卡参数 */
        rkos_strncpy((char *)aiAttr.u8CardName, dev->soundCard, sizeof(aiAttr.u8CardName) - 1);
        /* soundCard.channels: 声卡硬件通道数 */
        aiAttr.soundCard.channels = dev->channels;
        aiAttr.soundCard.sampleRate = dev->sampleRate;

        /* 将原始位宽数值转换为 Rockit 枚举值 */
        /* AUDIO_BIT_WIDTH_8=0, AUDIO_BIT_WIDTH_16=1, AUDIO_BIT_WIDTH_24=2, AUDIO_BIT_WIDTH_32=3 */
        AUDIO_BIT_WIDTH_E bitWidthEnum;
        switch (dev->bitWidth) {
            case 8:  bitWidthEnum = AUDIO_BIT_WIDTH_8;  break;
            case 16: bitWidthEnum = AUDIO_BIT_WIDTH_16; break;
            case 24: bitWidthEnum = AUDIO_BIT_WIDTH_24; break;
            case 32: bitWidthEnum = AUDIO_BIT_WIDTH_32; break;
            default: bitWidthEnum = AUDIO_BIT_WIDTH_16; break;
        }
        aiAttr.soundCard.bitWidth = bitWidthEnum;
        aiAttr.enBitwidth = bitWidthEnum;
        aiAttr.u32EXFlag = 0;
        /* u32ChnCnt: AI 通道数量，固定为 2 */
        aiAttr.u32ChnCnt = 2;

        if (dev->channelMapEnable) {
            /* 启用多通道和声道映射 */
            aiAttr.bMultichnFlag = 1;
            for (ci = 0; ci < (int)dev->chnCount && ci < MAX_AI_CHANNELS_PER_DEV; ci++) {
                RockitAIChnConfig *chn = &dev->chnConfigs[ci];
                aiAttr.u8MapOutChns[ci] = chn->channelMap.chnCount;
                /* 使用声道映射配置 */
                for (int i = 0; i < (int)chn->channelMap.chnCount && i < MAX_CHN_CHNS; i++) {
                    aiAttr.u8MapChns[ci][i] = chn->channelMap.chnMap[i];
                }
                printf("[INFO]     Channel[%d] chn map: chnTracks=%u -> devTracks=[%u, %u, ...]\n",
                       ci, chn->channelMap.chnCount,
                       chn->channelMap.chnMap[0],
                       chn->channelMap.chnCount > 1 ? chn->channelMap.chnMap[1] : 0);
            }
        } else {
            /* 单声道模式 */
            RockitAIChnConfig *singleChn = &dev->chnConfigs[0];
            aiAttr.enSoundmode = (singleChn->channels == 1) ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;
        }
        /* 先设置第一个通道的参数作为设备公共属性 */
        RockitAIChnConfig *firstChn = &dev->chnConfigs[0];
        aiAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)firstChn->sampleRate;
        aiAttr.u32PtNumPerFrm = (((firstChn->sampleRate == 0)? dev->sampleRate: firstChn->sampleRate) *
                                  firstChn->frameTime * firstChn->channels * firstChn->bitWidth) / 8000;
        aiAttr.u32FrmNum = firstChn->bufferCount;

        ret = RK_MPI_AI_SetPubAttr(dev->devId, &aiAttr);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RK_MPI_AI_SetPubAttr failed for dev %d: %#x\n", dev->devId, ret);
            goto cleanup_devices;
        }
        printf("[INFO]   AI SetPubAttr success for dev %d: cardName=%s\n",
               dev->devId, aiAttr.u8CardName);

        /* 使能 AI 设备 */
        ret = RK_MPI_AI_Enable(dev->devId);
        if (ret != RK_SUCCESS) {
            fprintf(stderr, "[ERROR] RK_MPI_AI_Enable failed for dev %d: %#x\n", dev->devId, ret);
            goto cleanup_devices;
        }
        state->aiState.devEnabled[di] = RKNS_TRUE;
        state->aiState.chnCount[di] = dev->chnCount;


        /* ========== 遍历该设备的所有通道 ========== */
        for (ci = 0; ci < (int)dev->chnCount && ci < MAX_AI_CHANNELS_PER_DEV; ci++) {
            RockitAIChnConfig *chn = &dev->chnConfigs[ci];

            printf("[INFO]     Creating AI channel[%d]: chnId=%d\n", ci, chn->chnId);

            rkos_memset(&chnParam, 0, sizeof(chnParam));
            /* 配置通道参数 */
            chnParam.s32UsrFrmDepth = (chn->depth > 0) ? chn->depth : 4;
            chnParam.enLoopbackMode = AUDIO_LOOPBACK_NONE;
            ret = RK_MPI_AI_SetChnParam(dev->devId, chn->chnId, &chnParam);
            if (ret != RK_SUCCESS) {
                fprintf(stderr, "[ERROR] RK_MPI_AI_SetChnParam failed for dev %d chn %d: %#x\n",
                        dev->devId, chn->chnId, ret);
                goto cleanup_channels;
            }

            /* 设置轨道模式（当声卡是2声道但通道是1声道时） */
            ret = RK_MPI_AI_SetTrackMode(dev->devId, (AUDIO_TRACK_MODE_E)chn->trackMode);
            if (ret != RK_SUCCESS) {
                fprintf(stderr, "[ERROR] RK_MPI_AI_SetTrackMode failed for dev %d: %#x\n",
                        dev->devId, ret);
                goto cleanup_channels;
            }
            printf("[INFO]     AI SetTrackMode success for dev %d: trackMode=%d\n",
                   dev->devId, chn->trackMode);

            /* 配置 VQE（声音质量增强） */
            if (chn->vqeConfigPath[0]) {
                AI_VQE_CONFIG_S stAiVqeConfig;
                rkos_memset(&stAiVqeConfig, 0, sizeof(AI_VQE_CONFIG_S));
                stAiVqeConfig.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
                rkos_strncpy((char *)stAiVqeConfig.aCfgFile, chn->vqeConfigPath, sizeof(stAiVqeConfig.aCfgFile) - 1);

                /* VQE gap 固定为 16ms */
                RK_S32 s32VqeGapMs = 16;
                stAiVqeConfig.s32WorkSampleRate = chn->sampleRate;
                stAiVqeConfig.s32FrameSample = chn->sampleRate * s32VqeGapMs / 1000;

                ret = RK_MPI_AI_SetVqeAttr(dev->devId, chn->chnId, 0, 0, &stAiVqeConfig);
                if (ret != RK_SUCCESS) {
                    fprintf(stderr, "[ERROR] RK_MPI_AI_SetVqeAttr failed for dev %d chn %d: %#x\n",
                            dev->devId, chn->chnId, ret);
                    goto cleanup_channels;
                }

                /* 使能 VQE */
                ret = RK_MPI_AI_EnableVqe(dev->devId, chn->chnId);
                if (ret != RK_SUCCESS) {
                    fprintf(stderr, "[ERROR] RK_MPI_AI_EnableVqe failed for dev %d chn %d: %#x\n",
                            dev->devId, chn->chnId, ret);
                    goto cleanup_channels;
                }
                printf("[INFO]     AI VQE enabled: devId=%d, chnId=%d, config=%s\n",
                       dev->devId, chn->chnId, chn->vqeConfigPath);
            }

            /* 使能 AI 通道 */
            ret = RK_MPI_AI_EnableChn(dev->devId, chn->chnId);
            if (ret != RK_SUCCESS) {
                fprintf(stderr, "[ERROR] RK_MPI_AI_EnableChn failed for dev %d chn %d: %#x\n",
                        dev->devId, chn->chnId, ret);
                goto cleanup_channels;
            }

            state->aiState.chnEnabled[di][ci] = RKNS_TRUE;
            printf("[INFO]     AI channel enabled: devId=%d, chnId=%d\n", dev->devId, chn->chnId);
        }
    }

    /* 使用第一个设备和第一个通道作为模块的主通道 */
    if (cfg->devCount > 0 && cfg->devConfigs[0].chnCount > 0) {
        state->chn.s32DevId = cfg->devConfigs[0].devId;
        state->chn.s32ChnId = cfg->devConfigs[0].chnConfigs[0].chnId;
    }
    state->created = RKNS_TRUE;
    printf("[INFO]   AI module created successfully with %u devices\n", cfg->devCount);
    return RKNS_SUCCESS;

cleanup_channels:
    /* 清理当前设备的已创建通道 */
    {
        RockitAIDevConfig *cleanupDev = &cfg->devConfigs[di];
        for (int c = ci - 1; c >= 0; c--) {
            RockitAIChnConfig *cleanupChn = &cleanupDev->chnConfigs[c];
            RK_MPI_AI_DisableChn(cleanupDev->devId, cleanupChn->chnId);
            state->aiState.chnEnabled[di][c] = RKNS_FALSE;
        }
    }

cleanup_devices:
    /* 清理已创建的设备 */
    for (int d = di; d >= 0; d--) {
        RockitAIDevConfig *cleanupDev = &cfg->devConfigs[d];
        if (state->aiState.devEnabled[d]) {
            /* 清理该设备的所有通道 */
            for (int c = 0; c < (int)cleanupDev->chnCount && c < MAX_AI_CHANNELS_PER_DEV; c++) {
                if (state->aiState.chnEnabled[d][c]) {
                    RockitAIChnConfig *cleanupChn = &cleanupDev->chnConfigs[c];
                    RK_MPI_AI_DisableChn(cleanupDev->devId, cleanupChn->chnId);
                    state->aiState.chnEnabled[d][c] = RKNS_FALSE;
                }
            }
            RK_MPI_AI_Disable(cleanupDev->devId);
            state->aiState.devEnabled[d] = RKNS_FALSE;
        }
    }
    return RKNS_FAILURE;
}

RKNS_S32 rockit_ai_destroy(const RKNSLaunchModule *module,
                                   RKNSLaunchModuleState *state) {
    RockitAIConfig *cfg = MODULE_CONFIG(module, RockitAIConfig);
    int di, ci;

    printf("[INFO]   Destroying AI module with %u devices\n", cfg->devCount);

    /* 清理所有设备和通道 */
    if (cfg->devCount > 0 && cfg->devConfigs != NULL) {
        for (di = (int)cfg->devCount - 1; di >= 0; di--) {
            RockitAIDevConfig *dev = &cfg->devConfigs[di];

            if (!state->aiState.devEnabled[di]) {
                continue;
            }

            /* 先禁用该设备的所有通道 */
            for (ci = (int)state->aiState.chnCount[di] - 1; ci >= 0; ci--) {
                if (state->aiState.chnEnabled[di][ci]) {
                    RockitAIChnConfig *chn = &dev->chnConfigs[ci];
                    printf("[INFO]     Disabling AI channel: devId=%d, chnId=%d\n",
                           dev->devId, chn->chnId);
                    RK_MPI_AI_DisableChn(dev->devId, chn->chnId);
                    state->aiState.chnEnabled[di][ci] = RKNS_FALSE;
                }
            }

            /* 禁用设备 */
            printf("[INFO]   Disabling AI device: devId=%d\n", dev->devId);
            RK_MPI_AI_Disable(dev->devId);
            state->aiState.devEnabled[di] = RKNS_FALSE;
        }
    }

    /* 释放动态分配的内存 */
    if (cfg->devConfigs != NULL) {
        for (di = 0; di < (int)cfg->devCount; di++) {
            if (cfg->devConfigs[di].chnConfigs != NULL) {
                rkos_kfree(cfg->devConfigs[di].chnConfigs);
                cfg->devConfigs[di].chnConfigs = NULL;
            }
        }
        rkos_kfree(cfg->devConfigs);
        cfg->devConfigs = NULL;
    }
    cfg->devCount = 0;

    state->created = RKNS_FALSE;
    printf("[INFO]   AI module destroyed\n");
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_ai_start(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    const RockitAIConfig *cfg = MODULE_CONFIG(module, RockitAIConfig);
    RK_S32 ret;
    int di, ci;

    if (cfg->devCount == 0 || cfg->devConfigs == NULL) {
        fprintf(stderr, "[ERROR] No device config available\n");
        return RKNS_FAILURE;
    }

    printf("[INFO]   Starting AI module with %u devices\n", cfg->devCount);

    /* 启动所有设备和通道的数据读取 */
    for (di = 0; di < (int)cfg->devCount && di < MAX_AI_DEVICES; di++) {
        RockitAIDevConfig *dev = &cfg->devConfigs[di];

        if (!state->aiState.devEnabled[di]) {
            continue;
        }

        for (ci = 0; ci < (int)dev->chnCount && ci < MAX_AI_CHANNELS_PER_DEV; ci++) {
            if (!state->aiState.chnEnabled[di][ci]) {
                continue;
            }

            RockitAIChnConfig *chn = &dev->chnConfigs[ci];
            printf("[INFO]     Enabling data read for dev %d chn %d\n", dev->devId, chn->chnId);

            ret = RK_MPI_AI_EnableDataRead(dev->devId, chn->chnId);
            if (ret != RK_SUCCESS) {
                fprintf(stderr, "[ERROR] RK_MPI_AI_EnableDataRead failed for dev %d chn %d: %#x\n",
                        dev->devId, chn->chnId, ret);
                /* 继续尝试其他通道 */
            }
        }
    }

    state->started = RKNS_TRUE;
    printf("[INFO]   AI module started\n");
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_ai_stop(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    const RockitAIConfig *cfg = MODULE_CONFIG(module, RockitAIConfig);
    RK_S32 ret;
    int di, ci;

    if (cfg->devCount > 0 && cfg->devConfigs != NULL) {
        printf("[INFO]   Stopping AI module with %u devices\n", cfg->devCount);

        /* 停止所有设备和通道的数据读取 */
        for (di = 0; di < (int)cfg->devCount && di < MAX_AI_DEVICES; di++) {
            RockitAIDevConfig *dev = &cfg->devConfigs[di];

            if (!state->aiState.devEnabled[di]) {
                continue;
            }

            for (ci = 0; ci < (int)dev->chnCount && ci < MAX_AI_CHANNELS_PER_DEV; ci++) {
                if (!state->aiState.chnEnabled[di][ci]) {
                    continue;
                }

                RockitAIChnConfig *chn = &dev->chnConfigs[ci];
                printf("[INFO]     Disabling data read for dev %d chn %d\n", dev->devId, chn->chnId);

                ret = RK_MPI_AI_DisableDataRead(dev->devId, chn->chnId);
                if (ret != RK_SUCCESS) {
                    fprintf(stderr, "[WARNING] RK_MPI_AI_DisableDataRead failed for dev %d chn %d: %#x\n",
                            dev->devId, chn->chnId, ret);
                }
            }
        }
    }

    state->started = RKNS_FALSE;
    printf("[INFO]   AI module stopped\n");
    return RKNS_SUCCESS;
}

/* ============================================================
 * 模块注册表
 * ============================================================ */

static const RKNSLaunchModuleOps g_moduleOps[] = {
    /* RKNS 模块 */
    { RKNS_LAUNCH_MOD_INPUT,    "INPUT",    input_parse_config,
      input_create,    input_destroy,    input_start,    input_stop    },
    { RKNS_LAUNCH_MOD_INFER,    "INFER",    infer_parse_config,
      infer_create,    infer_destroy,    infer_start,    infer_stop    },
    { RKNS_LAUNCH_MOD_GRAPHICS, "GRAPHICS", graphics_parse_config,
      graphics_create, graphics_destroy, graphics_start, graphics_stop },
    { RKNS_LAUNCH_MOD_OUTPUT,   "OUTPUT",   output_parse_config,
      output_create,   output_destroy,   output_start,   output_stop   },
    /* Rockit 模块 */
    { RKNS_LAUNCH_MOD_RK_VI,    "VI",       rockit_vi_parse_config,
      rockit_vi_create,    rockit_vi_destroy,    rockit_vi_start,    rockit_vi_stop },
    { RKNS_LAUNCH_MOD_RK_VO,    "VO",       rockit_vo_parse_config,
      rockit_vo_create,    rockit_vo_destroy,    rockit_vo_start,    rockit_vo_stop },
    { RKNS_LAUNCH_MOD_RK_AI,    "AI",       rockit_ai_parse_config,
      rockit_ai_create,    rockit_ai_destroy,    rockit_ai_start,    rockit_ai_stop },
};
#define MODULE_OPS_COUNT  (sizeof(g_moduleOps) / sizeof(g_moduleOps[0]))

const RKNSLaunchModuleOps *rkns_launch_find_module_ops(RKNSLaunchModId modId) {
    RKNS_U32 i;
    for (i = 0; i < MODULE_OPS_COUNT; i++) {
        if (g_moduleOps[i].modId == modId) {
            return &g_moduleOps[i];
        }
    }
    return NULL;
}

/* ID 映射辅助函数 */
RKNS_BOOL rkns_launch_is_rockit_module(RKNSLaunchModId id) {
    return (id >= 100 && id < RKNS_LAUNCH_MOD_BUTT) ? RKNS_TRUE : RKNS_FALSE;
}

RKNS_S32 rkns_launch_to_mpp_id(RKNSLaunchModId id) {
    switch (id) {
    case RKNS_LAUNCH_MOD_RK_VI:   return RK_ID_VI;
    case RKNS_LAUNCH_MOD_RK_VO:   return RK_ID_VO;
    case RKNS_LAUNCH_MOD_RK_VPSS: return RK_ID_VPSS;
    case RKNS_LAUNCH_MOD_RK_RGN:  return RK_ID_RGN;
    case RKNS_LAUNCH_MOD_RK_AI:   return RK_ID_AI;
    default: return -1;
    }
}

RKNS_S32 rkns_launch_to_rkns_id(RKNSLaunchModId id) {
    return (id < 100) ? (RKNS_S32)id : -1;
}

/* ============================================================
 * Pipeline 生命周期函数（注册表驱动）
 * ============================================================ */

/**
 * @brief 查找所有 enabled 的 pipeline
 */
static RKNS_U32 find_all_enabled_pipelines(const RKNSLaunchConfig *config,
                                           RKNSLaunchPipelineContext *contexts,
                                           RKNS_U32 maxContexts) {
    RKNS_U32 i, count = 0;

    for (i = 0; i < config->pipelineCount && count < maxContexts; i++) {
        if (config->pipelines[i].enabled) {
            contexts[count].desc = &config->pipelines[i];
            contexts[count].created = RKNS_FALSE;
            contexts[count].started = RKNS_FALSE;
            rkos_memset(&contexts[count].state, 0, sizeof(RKNSLaunchPipeline));
            count++;
        }
    }
    return count;
}

/**
 * @brief 绑定流水线（遍历 YAML bindings）
 */
static RKNS_S32 bind_pipeline(const RKNSLaunchPipelineDesc *pl,
                               RKNSLaunchPipeline *pipeline,
                               RKNS_BOOL verbose) {
    RKNS_U32 i;

    for (i = 0; i < pl->bindingCount; i++) {
        const RKNSLaunchBinding *b = &pl->bindings[i];
        RKNS_S32 ret;

        if (verbose) {
            printf("[INFO]   Binding %s: src=(mod=%d,dev=%d,chn=%d) dst=(mod=%d,dev=%d,chn=%d)\n",
                   b->name,
                   b->srcChn.enModId, b->srcChn.s32DevId, b->srcChn.s32ChnId,
                   b->dstChn.enModId, b->dstChn.s32DevId, b->dstChn.s32ChnId);
        }

        if (pl->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) {
            /* Rockit 绑定：翻译统一 ID 为 MPP_CHN_S */
            MPP_CHN_S srcChn, dstChn;
            srcChn.enModId = rkns_launch_to_mpp_id((RKNSLaunchModId)b->srcChn.enModId);
            srcChn.s32DevId = b->srcChn.s32DevId;
            srcChn.s32ChnId = b->srcChn.s32ChnId;
            dstChn.enModId = rkns_launch_to_mpp_id((RKNSLaunchModId)b->dstChn.enModId);
            dstChn.s32DevId = b->dstChn.s32DevId;
            dstChn.s32ChnId = b->dstChn.s32ChnId;
            ret = RK_MPI_SYS_Bind(&srcChn, &dstChn);
        } else {
            /* RKNS 绑定 */
            ret = RKNS_SYS_Bind(&b->srcChn, &b->dstChn);
        }

        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] Bind %s failed: %d\n", b->name, ret);
            return ret;
        }
        pipeline->bindingsBound[i] = RKNS_TRUE;

        if (verbose) {
            printf("[INFO]   %s bound successfully\n", b->name);
        }
    }

    return RKNS_SUCCESS;
}

/**
 * @brief 解绑流水线（反向遍历 bindings）
 */
static void unbind_pipeline(const RKNSLaunchPipelineDesc *pl,
                             RKNSLaunchPipeline *pipeline,
                             RKNS_BOOL verbose) {
    RKNS_S32 i;

    for (i = (RKNS_S32)pl->bindingCount - 1; i >= 0; i--) {
        if (pipeline->bindingsBound[i]) {
            const RKNSLaunchBinding *b = &pl->bindings[i];

            if (pl->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) {
                MPP_CHN_S srcChn, dstChn;
                srcChn.enModId = rkns_launch_to_mpp_id((RKNSLaunchModId)b->srcChn.enModId);
                srcChn.s32DevId = b->srcChn.s32DevId;
                srcChn.s32ChnId = b->srcChn.s32ChnId;
                dstChn.enModId = rkns_launch_to_mpp_id((RKNSLaunchModId)b->dstChn.enModId);
                dstChn.s32DevId = b->dstChn.s32DevId;
                dstChn.s32ChnId = b->dstChn.s32ChnId;
                RK_MPI_SYS_UnBind(&srcChn, &dstChn);
            } else {
                RKNS_SYS_UnBind(&b->srcChn, &b->dstChn);
            }

            pipeline->bindingsBound[i] = RKNS_FALSE;
            if (verbose) {
                printf("[INFO]   %s unbound\n", b->name);
            }
        }
    }
}

/**
 * @brief 创建完整流水线（注册表驱动，正向遍历 modules）
 */
static RKNS_S32 create_pipeline(const RKNSLaunchPipelineDesc *pl,
                                 RKNSLaunchPipeline *pipeline,
                                 RKNS_BOOL verbose) {
    RKNS_U32 i;
    RKNS_S32 ret;
    const char *typeName = (pl->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) ? "Rockit" : "RKNNStream";

    printf("\n========== Creating %s Pipeline ==========\n", typeName);
    printf("[INFO] Pipeline: %s (%u modules, %u bindings)\n",
           pl->name, pl->moduleCount, pl->bindingCount);

    rkos_memset(pipeline, 0, sizeof(RKNSLaunchPipeline));
    pipeline->moduleCount = pl->moduleCount;

    for (i = 0; i < pl->moduleCount; i++) {
        const RKNSLaunchModule *mod = &pl->modules[i];
        const RKNSLaunchModuleOps *ops = rkns_launch_find_module_ops(mod->modId);

        if (!ops || !ops->create) {
            fprintf(stderr, "[WARN] No create ops for module %s (modId=%d), skipping\n",
                    mod->name, mod->modId);
            continue;
        }

        if (!mod->configParsed) {
            fprintf(stderr, "[WARN] Module %s config not parsed, using defaults\n",
                    mod->name);
        }

        ret = ops->create(mod, &pipeline->moduleStates[i]);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to create module %s\n", mod->name);
            /* 反向销毁已创建的模块 */
            while (i-- > 0) {
                const RKNSLaunchModule *prev = &pl->modules[i];
                const RKNSLaunchModuleOps *prev_ops = rkns_launch_find_module_ops(prev->modId);
                if (prev_ops && prev_ops->destroy) {
                    prev_ops->destroy(prev, &pipeline->moduleStates[i]);
                }
            }
            return ret;
        }
        if (verbose) {
            printf("[INFO]   [%s] Module[%u] %s created (dev=%d, chn=%d)\n",
                   pl->name, i, mod->name, mod->devId, mod->chnId);
        }
    }

    /* 绑定 */
    ret = bind_pipeline(pl, pipeline, verbose);
    if (ret != RKNS_SUCCESS) {
        /* 绑定失败，销毁所有模块 */
        for (i = 0; i < pl->moduleCount; i++) {
            const RKNSLaunchModule *mod = &pl->modules[i];
            const RKNSLaunchModuleOps *ops = rkns_launch_find_module_ops(mod->modId);
            if (ops && ops->destroy) {
                ops->destroy(mod, &pipeline->moduleStates[i]);
            }
        }
        return ret;
    }

    {
        const char *typeName = (pl->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) ? "Rockit" : "RKNNStream";
        printf("========== %s Pipeline Created Successfully ==========\n\n", typeName);
    }
    return RKNS_SUCCESS;
}

/**
 * @brief 启动流水线（反向遍历：OUTPUT -> INPUT）
 */
static RKNS_S32 start_pipeline(const RKNSLaunchPipelineDesc *pl,
                                RKNSLaunchPipeline *pipeline,
                                RKNS_BOOL verbose) {
    RKNS_S32 i;

    for (i = (RKNS_S32)pl->moduleCount - 1; i >= 0; i--) {
        const RKNSLaunchModule *mod = &pl->modules[i];
        const RKNSLaunchModuleOps *ops = rkns_launch_find_module_ops(mod->modId);

        if (ops && ops->start && pipeline->moduleStates[i].created) {
            RKNS_S32 ret = ops->start(mod, &pipeline->moduleStates[i]);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] Failed to start module %s: %d\n",
                        mod->name, ret);
                return ret;
            }
            if (verbose) {
                printf("[INFO]   [%s] Module[%d] %s started\n",
                       pl->name, i, mod->name);
            }
        }
    }
    return RKNS_SUCCESS;
}

/**
 * @brief 停止流水线（正向遍历：INPUT -> OUTPUT）
 */
static void stop_pipeline(const RKNSLaunchPipelineDesc *pl,
                           RKNSLaunchPipeline *pipeline,
                           RKNS_BOOL verbose) {
    RKNS_U32 i;

    for (i = 0; i < pl->moduleCount; i++) {
        const RKNSLaunchModule *mod = &pl->modules[i];
        const RKNSLaunchModuleOps *ops = rkns_launch_find_module_ops(mod->modId);

        if (ops && ops->stop && pipeline->moduleStates[i].started) {
            RKNS_S32 ret = ops->stop(mod, &pipeline->moduleStates[i]);
            if (ret != RKNS_SUCCESS && verbose) {
                fprintf(stderr, "[WARN] [%s] Failed to stop module %s: %d\n",
                        pl->name, mod->name, ret);
            } else if (verbose) {
                printf("[INFO]   [%s] Module[%u] %s stopped\n",
                       pl->name, i, mod->name);
            }
        }
    }
}

/**
 * @brief 销毁流水线（反向遍历）
 */
static void destroy_pipeline(const RKNSLaunchPipelineDesc *pl,
                              RKNSLaunchPipeline *pipeline,
                              RKNS_BOOL verbose) {
    RKNS_S32 i;

    for (i = (RKNS_S32)pl->moduleCount - 1; i >= 0; i--) {
        const RKNSLaunchModule *mod = &pl->modules[i];
        const RKNSLaunchModuleOps *ops = rkns_launch_find_module_ops(mod->modId);

        if (ops && ops->destroy && pipeline->moduleStates[i].created) {
            RKNS_S32 ret = ops->destroy(mod, &pipeline->moduleStates[i]);
            if (ret != RKNS_SUCCESS && verbose) {
                fprintf(stderr, "[WARN] [%s] Failed to destroy module %s: %d\n",
                        pl->name, mod->name, ret);
            } else if (verbose) {
                printf("[INFO]   [%s] Module[%d] %s destroyed\n",
                       pl->name, i, mod->name);
            }
        }
    }
}

/* ============================================================
 * 主函数
 * ============================================================ */

/**
 * @brief 主函数
 */
int main(int argc, char *argv[]) {
    RKNS_S32 ret;
    RKNSLaunchPipelineContext pipelineContexts[MAX_PIPELINES_RUNNING];
    RKNS_U32 pipelineCount = 0;
    RKNS_BOOL hasRockit = RKNS_FALSE;
    RKNS_BOOL hasRKNS = RKNS_FALSE;
    struct timespec start_time, current_time;
    int fd = STDIN_FILENO;
    long elapsed_seconds = 0;
    RKNS_U32 i;

    printf("=====================================================\n");
    printf("  RKNNStream Complete Application Launch\n");
    printf("  Version: 1.0.0\n");
    printf("=====================================================\n\n");

    // 初始化默认配置
    init_default_config(&g_config);

    // 解析命令行参数
    ret = parse_arguments(&g_config, argc, argv);
    if (ret != RKNS_SUCCESS) {
        return -1;
    }

    // 查找所有启用的 pipeline
    pipelineCount = find_all_enabled_pipelines(&g_config, pipelineContexts, MAX_PIPELINES_RUNNING);
    if (pipelineCount == 0) {
        fprintf(stderr, "[ERROR] No enabled pipeline found\n");
        return -1;
    }

    // 检测是否有 Rockit 和 RKNS pipeline
    for (i = 0; i < pipelineCount; i++) {
        if (pipelineContexts[i].desc->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) {
            hasRockit = RKNS_TRUE;
        } else {
            hasRKNS = RKNS_TRUE;
        }
    }

    // 打印启用的 pipeline
    printf("[INFO] Found %u enabled pipeline(s):\n", pipelineCount);
    for (i = 0; i < pipelineCount; i++) {
        const char *typeName = (pipelineContexts[i].desc->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) ? "Rockit" : "RKNNStream";
        printf("  [%u] %s (type: %s, modules: %u)\n", i,
               pipelineContexts[i].desc->name, typeName, pipelineContexts[i].desc->moduleCount);
    }

    // RKNNStream pipeline 的配置同步（仅对第一个 RKNSStream pipeline）
    for (i = 0; i < pipelineCount; i++) {
        if (pipelineContexts[i].desc->type == RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM) {
            if (pipelineContexts[i].desc->moduleCount > 0 &&
                pipelineContexts[i].desc->modules[0].configParsed) {
                sync_pipeline_desc_to_cli(pipelineContexts[i].desc, &g_config);
            }
            break;
        }
    }

    // 打印配置信息
    print_config(&g_config);

    // RKNSStream CLI 模式同步
    for (i = 0; i < pipelineCount; i++) {
        if (pipelineContexts[i].desc->type == RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM) {
            if (pipelineContexts[i].desc->moduleCount > 0 &&
                !pipelineContexts[i].desc->modules[0].configParsed) {
                sync_cli_to_pipeline_desc(&g_config, (RKNSLaunchPipelineDesc *)pipelineContexts[i].desc);
            }
            break;
        }
    }

    // 注册信号处理
    signal(SIGINT, signal_handler);

    // 按 pipeline 类型初始化系统
    if (hasRockit) {
        printf("[INFO] Initializing Rockit system...\n");
        ret = RK_MPI_SYS_Init();
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_SYS_Init failed: %d\n", ret);
            return -1;
        }
    }
    if (hasRKNS) {
        printf("[INFO] Initializing RKNS system...\n");
        ret = RKNS_SYS_Init();
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RKNS_SYS_Init failed: %d\n", ret);
            if (hasRockit) RK_MPI_SYS_Exit();
            return -1;
        }
    }
    printf("[INFO] System initialized successfully\n\n");

    // 设置终端为非阻塞模式
    set_nonblocking(fd);

    // 创建所有 pipeline
    for (i = 0; i < pipelineCount; i++) {
        ret = create_pipeline(pipelineContexts[i].desc, &pipelineContexts[i].state, g_config.verbose);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to create pipeline %s\n",
                    pipelineContexts[i].desc->name);
            goto cleanup;
        }
        pipelineContexts[i].created = RKNS_TRUE;
    }

    // 注意：绑定已在 create_pipeline 内部完成，不需要重复绑定

    // 启动所有 pipeline（反向顺序）
    printf("[INFO] Starting pipelines...\n");
    for (i = pipelineCount - 1; i < pipelineCount; i--) {
        ret = start_pipeline(pipelineContexts[i].desc, &pipelineContexts[i].state, g_config.verbose);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to start pipeline %s\n",
                    pipelineContexts[i].desc->name);
            goto cleanup;
        }
        pipelineContexts[i].started = RKNS_TRUE;
    }
    printf("[INFO] All pipelines started successfully\n\n");

    printf("=====================================================\n");
    printf("  %u Pipeline(s) running...\n", pipelineCount);
    printf("  Press 'q' or Ctrl+C to exit\n");
    printf("=====================================================\n\n");

    // 获取开始时间
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // 主循环
    while (g_running && !g_exit) {
        // 检查超时
        if (g_config.timeout > 0) {
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            elapsed_seconds = current_time.tv_sec - start_time.tv_sec;

            if (elapsed_seconds >= g_config.timeout) {
                printf("\n[INFO] Timeout reached (%d seconds)\n", g_config.timeout);
                break;
            }
        }

        // 检查用户输入
        char c;
        int key_ret = read(fd, &c, 1);
        if (key_ret > 0) {
            if (c == 'q' || c == 'Q') {
                printf("\n[INFO] User requested exit\n");
                break;
            }
        } else if (key_ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // 读错误
            fprintf(stderr, "\n[WARN] Read error: %s\n", strerror(errno));
            break;
        }

        // 短暂休眠避免 CPU 占用过高
        usleep(100000);  // 100ms
    }

    // 停止所有 pipeline（正向顺序）
    printf("\n[INFO] Stopping pipelines...\n");
    for (i = 0; i < pipelineCount; i++) {
        if (pipelineContexts[i].started) {
            stop_pipeline(pipelineContexts[i].desc, &pipelineContexts[i].state, g_config.verbose);
            pipelineContexts[i].started = RKNS_FALSE;
        }
    }

    // 解绑所有 pipeline
    printf("[INFO] Unbinding pipelines...\n");
    for (i = 0; i < pipelineCount; i++) {
        if (pipelineContexts[i].created) {
            unbind_pipeline(pipelineContexts[i].desc, &pipelineContexts[i].state, g_config.verbose);
        }
    }

    // 销毁所有 pipeline（反向顺序）
    printf("[INFO] Destroying pipelines...\n");
    for (i = 0; i < pipelineCount; i++) {
        if (pipelineContexts[i].created) {
            destroy_pipeline(pipelineContexts[i].desc, &pipelineContexts[i].state, g_config.verbose);
            pipelineContexts[i].created = RKNS_FALSE;
        }
    }

    printf("[INFO] All pipelines cleanup completed\n\n");

cleanup:
    // 恢复终端设置
    restore_terminal(fd);

    // 退出系统
    printf("[INFO] Exiting RKNS system...\n");
    ret = RKNS_SYS_Exit();
    if (ret != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] RKNS_SYS_Exit failed: %d\n", ret);
    }

    if (hasRockit) {
        printf("[INFO] Exiting Rockit system...\n");
        ret = RK_MPI_SYS_Exit();
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_SYS_Exit failed: %d\n", ret);
        }
    }

    printf("[INFO] System exited successfully\n");
    printf("\n=====================================================\n");
    printf("  RKNNStream Launch Exited Normally\n");
    printf("=====================================================\n\n");

    return 0;
}
