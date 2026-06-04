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
#include <sys/ioctl.h>

#include "rkns.h"
#include "rkns_api_voice.h"
#include "rkns_api_business.h"
#include "rkns_api_text.h"
#include "rkns_api_memory.h"
#include "rkns_api_input.h"
#include "rkns_api_output.h"
#include "rk_osal.h"
#include "rkns_launch_config.h"
#include "rockx.h"
#include "rockx_module_type.h"
#include "rockx_value.h"
#include "rkns_buf.h"

/* Rockit MPI headers */
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_ai.h"
#include "rk_mpi_vpss.h"
#include "rk_comm_aio.h"
#include "rk_comm_vpss.h"
#include "rk_common.h"
#include "rk_comm_vo.h"

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
 * 前向声明
 * ============================================================ */
typedef struct _RKNSLaunchPipelineContext RKNSLaunchPipelineContext;

/* ============================================================
 * 全局变量
 * ============================================================ */

static volatile RKNS_BOOL g_running = RKNS_TRUE;
static volatile RKNS_BOOL g_exit = RKNS_FALSE;
static RKNSLaunchConfig g_config;
static struct termios g_old_termios;
static RKNSLaunchPipelineContext *g_pipeline_contexts = NULL;
static RKNS_U32 g_pipeline_count = 0;

/* ============================================================
 * 工具函数
 * ============================================================ */

/**
 * @brief 创建包含文本的 RKNS 帧（来自官方 rkns_llm_demo.c）
 */
static int create_string_value_frame(const char *s, RKNS_HANDLE *phBuf)
{
    RKNSMemFrame frame_info;
    size_t n;

    if (!s || !phBuf) {
        return -1;
    }
    n = strlen(s) + 1;

    memset(&frame_info, 0, sizeof(frame_info));
    frame_info.bufType = RKNS_BUF_TYPE_VALUE;
    frame_info.size = (RKNS_U32)n;
    frame_info.value.type = ROCKX_VALUE_TYPE_STRING;
    frame_info.value.dtype = ROCKX_DATA_TYPE_UINT8;
    frame_info.value.count = (RKNS_U32)(n - 1);
    frame_info.value.elemSize = sizeof(char);
    frame_info.value.needRelease = 0;

    *phBuf = RKNS_MEM_CreateFrame(&frame_info);
    if (!*phBuf) {
        return -1;
    }

    if (RKNS_MEM_GetBufInfo(*phBuf, &frame_info) != RKNS_SUCCESS) {
        RKNS_MEM_DestroyFrame(*phBuf);
        *phBuf = 0;
        return -1;
    }

    memcpy(frame_info.value.s, s, n);
    RKNS_MEM_FlushBufCache(*phBuf);
    return 0;
}

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
 * 聊天历史管理
 * ============================================================ */

#define MAX_HISTORY 100   // 最多保存100条历史记录
#define MAX_MESSAGE_LEN 4096

typedef enum {
    MSG_TYPE_USER,
    MSG_TYPE_LLM,
    MSG_TYPE_SYSTEM
} MessageType;

typedef struct {
    MessageType type;
    char content[MAX_MESSAGE_LEN];
} ChatMessage;

static ChatMessage g_chat_history[MAX_HISTORY];
static int g_history_count = 0;

// 添加聊天消息到历史记录
static void add_chat_message(MessageType type, const char *content) {
    if (g_history_count < MAX_HISTORY) {
        g_chat_history[g_history_count].type = type;
        strncpy(g_chat_history[g_history_count].content, content, MAX_MESSAGE_LEN - 1);
        g_chat_history[g_history_count].content[MAX_MESSAGE_LEN - 1] = '\0';
        g_history_count++;
    } else {
        // 如果已满，移除最早的一条，添加新的到末尾
        for (int i = 1; i < MAX_HISTORY; i++) {
            g_chat_history[i - 1] = g_chat_history[i];
        }
        g_chat_history[MAX_HISTORY - 1].type = type;
        strncpy(g_chat_history[MAX_HISTORY - 1].content, content, MAX_MESSAGE_LEN - 1);
        g_chat_history[MAX_HISTORY - 1].content[MAX_MESSAGE_LEN - 1] = '\0';
    }
}

// ANSI 转义序列辅助函数
static void move_cursor_up(int lines) {
    printf("\033[%dA", lines);
}

static void move_cursor_down(int lines) {
    printf("\033[%dB", lines);
}

static void move_cursor_to_col(int col) {
    printf("\033[%dG", col);
}

static void clear_line() {
    printf("\033[2K");
}

static void clear_screen() {
    printf("\033[2J\033[H");
}

// 获取终端行数
static int get_terminal_lines() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        return 24; // 默认24行
    }
    return w.ws_row;
}

// 计算文本需要的行数
static int count_text_lines(const char *text, int width) {
    int lines = 1;
    int current_len = 0;
    
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            lines++;
            current_len = 0;
        } else {
            current_len++;
            if (current_len >= width) {
                lines++;
                current_len = 0;
            }
        }
    }
    return lines;
}

// 重新绘制整个聊天界面
static void redraw_chat_interface(const char *input_buf, int input_pos) {
    int term_lines = get_terminal_lines();
    int term_cols = 80; // 默认列数
    printf("\033[%dF", term_lines); //移动光标到左上角
    printf("\n\033[J"); //清楚光标以下的所有内容
    // 清除屏幕并移动光标到左上角
    //clear_screen();
    
    // 打印标题
    printf("=====================================================\n");
    printf("  Chat with LLM (Press 'q' to exit)\n");
    printf("=====================================================\n\n");
    
    // 计算聊天区域可用的行数（减去标题行和输入行）
    int available_lines = term_lines - 7;
    
    // 从后往前打印历史记录，只显示能容纳的内容
    int start_idx = 0;
    int total_used_lines = 0;
    
    // 先计算需要显示哪些消息，跳过系统消息
    for (int i = g_history_count - 1; i >= 0; i--) {
        // 跳过系统消息
        if (g_chat_history[i].type == MSG_TYPE_SYSTEM) {
            continue;
        }
        
        int msg_lines = 0;
        switch (g_chat_history[i].type) {
            case MSG_TYPE_USER:
                msg_lines = 1 + count_text_lines(g_chat_history[i].content, term_cols - 6);
                break;
            case MSG_TYPE_LLM:
                msg_lines = 1 + count_text_lines(g_chat_history[i].content, term_cols - 6);
                break;
            default:
                continue;
        }
        
        if (total_used_lines + msg_lines <= available_lines) {
            total_used_lines += msg_lines;
            start_idx = i;
        } else {
            break;
        }
    }
    
    // 打印历史记录，跳过系统消息
    for (int i = start_idx; i < g_history_count; i++) {
        // 跳过系统消息
        if (g_chat_history[i].type == MSG_TYPE_SYSTEM) {
            continue;
        }
        
        switch (g_chat_history[i].type) {
            case MSG_TYPE_USER:
                printf("\033[32m[You]\033[0m: %s\n", g_chat_history[i].content);
                break;
            case MSG_TYPE_LLM:
                printf("\033[34m[LLM]\033[0m: %s\n", g_chat_history[i].content);
                break;
            default:
                break;
        }
    }
    
    // 打印分隔线和输入提示符
    printf("\n");
    printf("-----------------------------------------------------\n");
    printf("> %s", input_buf);
    
    // 移动光标到正确的位置
    if (input_pos < (int)strlen(input_buf)) {
        // 需要把光标移回到 input_pos 的位置
        int move_back = (int)strlen(input_buf) - input_pos;
        if (move_back > 0) {
            printf("\033[%dD", move_back); // 向左移动 move_back 列
        }
    }
    
    fflush(stdout);
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
            break;

        case 'i':  // input type
            break;

        case 0x100:  // input-path
            break;

        case 'W':  // width
            break;

        case 'H':  // height
            break;

        case 'F':  // fps
            break;

        case 'm':  // model type
            break;

        case 0x101:  // model-path
            break;

        case 'c':  // confidence
            break;

        case 0x102:  // enable-overlay
            break;

        case 0x103:  // disable-overlay
            break;

        case 'o':  // output type
            break;

        case 0x104:  // output-path
            break;

        case 0x105:  // watch-depth
            break;

        case 't':  // timeout
            break;

        case 0x106:  // profiling
            break;

        case 'v':  // verbose
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

    /* 检测 op_type 字段以确定输入类型 */
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
        param->rockit.modId = RK_ID_AI;
    } else if (strcmp(str, "VPSS_CAPTURE") == 0) {
        /* 视频输入模式 - Rockit VPSS 源 */
        param->sourceType = RKNS_INPUT_SOURCE_ROCKIT;

        /* 解析 source 对象 */
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.source.dev_id", base_path);
        param->rockit.devId = yaml_get_int(doc, path, 0);

        snprintf(path, sizeof(path), "%s.channels.0.chn_config.source.chn_id", base_path);
        param->rockit.chnId = yaml_get_int(doc, path, 0);

        /* 设置模块 ID 为 VPSS (RK_ID_VPSS = 5) */
        param->rockit.modId = RK_ID_VPSS;
    } else if (param->sourceType == RKNS_INPUT_SOURCE_APP) {
        /* APP 输入模式 */
        /* 解析 path 参数（如果有的话，没有则为 NULL） */
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.path", base_path);
        str = yaml_get_string(doc, path, "");
        if (strlen(str) > 0) {
            /* 如果有配置 path，设置它 */
            /* 注意：这里需要处理内存分配，暂时设置为 NULL 表示不自动发帧 */
            param->app.path = NULL;
        } else {
            /* 没有配置 path，设置为 NULL，不自动发帧 */
            param->app.path = NULL;
        }
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
    const char *source_type_str = "UNKNOWN";
    switch (param->sourceType) {
        case RKNS_INPUT_SOURCE_ROCKIT: source_type_str = "ROCKIT"; break;
        case RKNS_INPUT_SOURCE_GSTREAMER: source_type_str = "GSTREAMER"; break;
        case RKNS_INPUT_SOURCE_OPENCV: source_type_str = "OPENCV"; break;
        case RKNS_INPUT_SOURCE_APP: source_type_str = "APP"; break;
        case RKNS_INPUT_SOURCE_CUSTOM: source_type_str = "CUSTOM"; break;
        default: source_type_str = "UNKNOWN"; break;
    }
    printf("[INFO]   source_type: %d (%s)\n", param->sourceType, source_type_str);
    if (param->sourceType == RKNS_INPUT_SOURCE_ROCKIT) {
        if (param->rockit.modId == 10) {
            printf("[INFO]   op_type: AI_CAPTURE\n");
            printf("[INFO]   source.module: AI (modId=%d)\n", param->rockit.modId);
        } else if (param->rockit.modId == 5) {
            printf("[INFO]   op_type: VPSS_CAPTURE\n");
            printf("[INFO]   source.module: VPSS (modId=%d)\n", param->rockit.modId);
        }
        printf("[INFO]   source.dev_id: %d\n", param->rockit.devId);
        printf("[INFO]   source.chn_id: %d\n", param->rockit.chnId);
    } else if (param->sourceType == RKNS_INPUT_SOURCE_APP) {
        printf("[INFO]   app.path: %s\n", param->app.path ? param->app.path : "(NULL - no auto frame)");
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

/* ---------- VOICE 模块 ---------- */

RKNS_S32 voice_parse_config(YamlDocument *doc, const char *base_path,
                                      RKNSLaunchModule *module) {
    RKNSVoiceParam *param = MODULE_CONFIG(module, RKNSVoiceParam);
    char path[512];
    const char *str;

    rkos_memset(param, 0, sizeof(RKNSVoiceParam));

    /* 解析 type */
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.type", base_path);
    str = yaml_get_string(doc, path, "VAD");
    if (strcmp(str, "VAD") == 0) {
        param->type = RKNS_VOICE_TYPE_VAD;
    } else {
        param->type = RKNS_VOICE_TYPE_MAX;
    }

    /* 解析通用参数 */
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.buf_count", base_path);
    param->bufCount = yaml_get_int(doc, path, 10);
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.buf_size", base_path);
    param->bufSize = yaml_get_int(doc, path, 320);
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.depth", base_path);
    param->depth = yaml_get_int(doc, path, 1);
    snprintf(path, sizeof(path), "%s.channels.0.chn_config.input_queue_len", base_path);
    param->inputQueueLen = yaml_get_int(doc, path, 10);

    /* 解析 VAD 特有参数 */
    if (param->type == RKNS_VOICE_TYPE_VAD) {
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.energy_threshold", base_path);
        param->vadParam.energyThreshold = (float)yaml_get_float(doc, path, 0.1f);
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.min_silence_ms", base_path);
        param->vadParam.minSilenceMs = yaml_get_int(doc, path, 100);
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.min_voice_ms", base_path);
        param->vadParam.minVoiceMs = yaml_get_int(doc, path, 100);
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.frame_time", base_path);
        param->vadParam.frameDurationMs = yaml_get_int(doc, path, 10);
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.window_duration_ms", base_path);
        param->vadParam.windowDurationMs = yaml_get_int(doc, path, 20);
        snprintf(path, sizeof(path), "%s.channels.0.chn_config.step_duration_ms", base_path);
        param->vadParam.stepDurationMs = yaml_get_int(doc, path, 10);
    }

    /* 获取通道 ID */
    snprintf(path, sizeof(path), "%s.channels.0.chn_id", base_path);
    module->chnId = (RKNS_S32)yaml_get_int(doc, path, 0);

    /* 打印 VOICE 模块配置参数 */
    printf("[INFO] VOICE Module Config:\n");
    printf("[INFO]   chn_id: %d\n", module->chnId);
    printf("[INFO]   type: %s\n", param->type == RKNS_VOICE_TYPE_VAD ? "VAD" : "UNKNOWN");
    printf("[INFO]   buf_count: %d\n", param->bufCount);
    printf("[INFO]   buf_size: %d\n", param->bufSize);
    printf("[INFO]   depth: %d\n", param->depth);
    printf("[INFO]   input_queue_len: %d\n", param->inputQueueLen);
    if (param->type == RKNS_VOICE_TYPE_VAD) {
        printf("[INFO]   energy_threshold: %.2f\n", param->vadParam.energyThreshold);
        printf("[INFO]   min_silence_ms: %d\n", param->vadParam.minSilenceMs);
        printf("[INFO]   min_voice_ms: %d\n", param->vadParam.minVoiceMs);
        printf("[INFO]   frame_duration_ms: %d\n", param->vadParam.frameDurationMs);
        printf("[INFO]   window_duration_ms: %d\n", param->vadParam.windowDurationMs);
        printf("[INFO]   step_duration_ms: %d\n", param->vadParam.stepDurationMs);
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 voice_create(const RKNSLaunchModule *module,
                              RKNSLaunchModuleState *state) {
    RKNSVoiceParam *param = MODULE_CONFIG(module, RKNSVoiceParam);
    RKNS_S32 ret;

    ret = RKNS_VOICE_CreateChn(module->devId, module->chnId, param);
    if (ret != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] RKNS_VOICE_CreateChn failed: %d\n", ret);
        return ret;
    }
    state->chn.enModId = RKNS_ID_VOICE;
    state->chn.s32DevId = module->devId;
    state->chn.s32ChnId = module->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 voice_destroy(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    if (state->created) {
        RKNS_VOICE_DestroyChn(module->devId, module->chnId);
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 voice_start(const RKNSLaunchModule *module,
                             RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_VOICE_StartChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_TRUE;
    return ret;
}

RKNS_S32 voice_stop(const RKNSLaunchModule *module,
                            RKNSLaunchModuleState *state) {
    RKNS_S32 ret = RKNS_VOICE_StopChn(module->devId, module->chnId);
    if (ret == RKNS_SUCCESS) state->started = RKNS_FALSE;
    return ret;
}

/* ---------- BUSINESS 模块 ---------- */

RKNS_S32 business_parse_config(YamlDocument *doc, const char *base_path,
                                          RKNSLaunchModule *module) {
    BusinessConfig *cfg = MODULE_CONFIG(module, BusinessConfig);
    char path[512], grp_base[512], chn_base[512];
    const char *str;
    int g, c, ret = RKNS_SUCCESS;
    int grp_count = 0, chn_count;

    rkos_memset(cfg, 0, sizeof(BusinessConfig));

    /* 先统计 group 数量 */
    snprintf(path, sizeof(path), "%s.groups", base_path);
    grp_count = yaml_get_count(doc, path);
    if (grp_count <= 0) grp_count = 1;

    /* 动态分配 group 配置数组 */
    cfg->grpCount = grp_count;
    cfg->grpConfigs = (BusinessGroupConfig *)rkos_kmalloc(
        sizeof(BusinessGroupConfig) * grp_count);
    if (!cfg->grpConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for BUSINESS group configs\n");
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->grpConfigs, 0, sizeof(BusinessGroupConfig) * grp_count);

    /* 解析每个 group */
    for (g = 0; g < grp_count; g++) {
        BusinessGroupConfig *grp = &cfg->grpConfigs[g];
        snprintf(grp_base, sizeof(grp_base), "%s.groups.%d", base_path, g);

        /* 获取 group ID */
        snprintf(path, sizeof(path), "%s.grp_id", grp_base);
        grp->grpId = (RKNS_S32)yaml_get_int(doc, path, g);

        /* 初始化 param */
        rkos_memset(&grp->param, 0, sizeof(RKNSBusinessParam));

        /* 解析 type - 这是 group 的属性 */
        snprintf(path, sizeof(path), "%s.grp_config.type", grp_base);
        str = yaml_get_string(doc, path, "SWITCH");
        if (strcmp(str, "SPLITTER") == 0) {
            grp->param.type = RKNS_BUSINESS_TYPE_SPLITTER;
        } else if (strcmp(str, "MERGER") == 0) {
            grp->param.type = RKNS_BUSINESS_TYPE_MERGER;
        } else if (strcmp(str, "SWITCH") == 0) {
            grp->param.type = RKNS_BUSINESS_TYPE_SWITCH;
        } else {
            grp->param.type = RKNS_BUSINESS_TYPE_MAX;
        }

        /* 解析各类型的参数 */
        if (grp->param.type == RKNS_BUSINESS_TYPE_SPLITTER) {
            snprintf(path, sizeof(path), "%s.grp_config.output_count", grp_base);
            grp->param.splitter.outputCount = yaml_get_int(doc, path, 1);
        } else if (grp->param.type == RKNS_BUSINESS_TYPE_MERGER) {
            snprintf(path, sizeof(path), "%s.grp_config.input_count", grp_base);
            grp->param.merger.inputCount = yaml_get_int(doc, path, 1);
        } else if (grp->param.type == RKNS_BUSINESS_TYPE_SWITCH) {
            snprintf(path, sizeof(path), "%s.grp_config.frame_time", grp_base);
            grp->param.switchParam.frameDurationMs = yaml_get_int(doc, path, 10);
            snprintf(path, sizeof(path), "%s.grp_config.mode", grp_base);
            str = yaml_get_string(doc, path, "ONOFF");
            if (strcmp(str, "BYPASS") == 0) {
                grp->param.switchParam.mode = RKNS_BUSINESS_SWITCH_MODE_BYPASS;
            } else if (strcmp(str, "ONOFF") == 0) {
                grp->param.switchParam.mode = RKNS_BUSINESS_SWITCH_MODE_ONOFF;
            } else if (strcmp(str, "RECOMBINE") == 0) {
                grp->param.switchParam.mode = RKNS_BUSINESS_SWITCH_MODE_RECOMBINE;
            }
        }

        /* 使用 yaml_get_count 获取该 group 的 channels 数量 */
        snprintf(path, sizeof(path), "%s.channels", grp_base);
        chn_count = yaml_get_count(doc, path);
        if (chn_count <= 0) {
            chn_count = 1;
        }

        grp->chnCount = chn_count;
        if (chn_count > 0) {
            /* 动态分配该 group 的 channel 配置数组 */
            grp->chnConfigs = (BusinessChannelConfig *)rkos_kmalloc(
                sizeof(BusinessChannelConfig) * chn_count);
            if (!grp->chnConfigs) {
                fprintf(stderr, "[ERROR] Failed to allocate memory for BUSINESS channel configs\n");
                /* 清理已分配的内存 */
                for (int j = 0; j <= g; j++) {
                    BusinessGroupConfig *temp_grp = &cfg->grpConfigs[j];
                    if (temp_grp->chnConfigs) {
                        rkos_kfree(temp_grp->chnConfigs);
                        temp_grp->chnConfigs = NULL;
                    }
                }
                rkos_kfree(cfg->grpConfigs);
                cfg->grpConfigs = NULL;
                cfg->grpCount = 0;
                return RKNS_FAILURE;
            }
            rkos_memset(grp->chnConfigs, 0, sizeof(BusinessChannelConfig) * chn_count);

            /* 解析该 group 的每个 channel */
            for (c = 0; c < chn_count; c++) {
                BusinessChannelConfig *chn = &grp->chnConfigs[c];
                snprintf(chn_base, sizeof(chn_base), "%s.channels.%d", grp_base, c);

                /* 获取 channel ID */
                snprintf(path, sizeof(path), "%s.chn_id", chn_base);
                chn->chnId = (RKNS_S32)yaml_get_int(doc, path, c);

                /* channel 暂时没有特殊的 chn_config 配置，就简单解析 */
            }
        }

        /* 打印 BUSINESS group 配置参数 */
        printf("[INFO] BUSINESS Group %d Config:\n", g);
        printf("[INFO]   grp_id: %d\n", grp->grpId);
        printf("[INFO]   type: ");
        if (grp->param.type == RKNS_BUSINESS_TYPE_SPLITTER) printf("SPLITTER\n");
        else if (grp->param.type == RKNS_BUSINESS_TYPE_MERGER) printf("MERGER\n");
        else if (grp->param.type == RKNS_BUSINESS_TYPE_SWITCH) printf("SWITCH\n");
        else printf("UNKNOWN\n");
        if (grp->param.type == RKNS_BUSINESS_TYPE_SPLITTER) {
            printf("[INFO]   output_count: %d\n", grp->param.splitter.outputCount);
        } else if (grp->param.type == RKNS_BUSINESS_TYPE_MERGER) {
            printf("[INFO]   input_count: %d\n", grp->param.merger.inputCount);
        } else if (grp->param.type == RKNS_BUSINESS_TYPE_SWITCH) {
            printf("[INFO]   frame_time: %d\n", grp->param.switchParam.frameDurationMs);
            printf("[INFO]   mode: ");
            if (grp->param.switchParam.mode == RKNS_BUSINESS_SWITCH_MODE_BYPASS) printf("BYPASS\n");
            else if (grp->param.switchParam.mode == RKNS_BUSINESS_SWITCH_MODE_ONOFF) printf("ONOFF\n");
            else if (grp->param.switchParam.mode == RKNS_BUSINESS_SWITCH_MODE_RECOMBINE) printf("RECOMBINE\n");
        }
        printf("[INFO]   channel_count: %d\n", grp->chnCount);
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

static void business_free_config(BusinessConfig *cfg) {
    if (cfg->grpConfigs) {
        for (int j = 0; j < (int)cfg->grpCount; j++) {
            BusinessGroupConfig *grp = &cfg->grpConfigs[j];
            if (grp->chnConfigs) {
                rkos_kfree(grp->chnConfigs);
                grp->chnConfigs = NULL;
            }
        }
        rkos_kfree(cfg->grpConfigs);
        cfg->grpConfigs = NULL;
    }
    cfg->grpCount = 0;
}

RKNS_S32 business_create(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    BusinessConfig *cfg = MODULE_CONFIG(module, BusinessConfig);
    RKNSLaunchBusinessState *biz_state = &state->bizState;
    int g, c, ret;

    rkos_memset(biz_state, 0, sizeof(RKNSLaunchBusinessState));

    /* 初始化 group count */
    biz_state->grpCount = cfg->grpCount;
    if (biz_state->grpCount == 0) {
        fprintf(stderr, "[ERROR] No BUSINESS groups to create\n");
        business_free_config(cfg);
        return RKNS_FAILURE;
    }

    /* 动态分配 group enable 数组 */
    biz_state->grpEnabled = (RKNS_BOOL *)rkos_kmalloc(
        sizeof(RKNS_BOOL) * biz_state->grpCount);
    if (!biz_state->grpEnabled) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for BUSINESS group state\n");
        business_free_config(cfg);
        return RKNS_FAILURE;
    }
    rkos_memset(biz_state->grpEnabled, 0, sizeof(RKNS_BOOL) * biz_state->grpCount);

    /* 动态分配 channel count 数组 */
    biz_state->chnCount = (RKNS_U32 *)rkos_kmalloc(
        sizeof(RKNS_U32) * biz_state->grpCount);
    if (!biz_state->chnCount) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for BUSINESS channel count\n");
        rkos_kfree(biz_state->grpEnabled);
        biz_state->grpEnabled = NULL;
        business_free_config(cfg);
        return RKNS_FAILURE;
    }
    rkos_memset(biz_state->chnCount, 0, sizeof(RKNS_U32) * biz_state->grpCount);

    /* 动态分配 channel enable 二维数组 */
    biz_state->chnEnabled = (RKNS_BOOL **)rkos_kmalloc(
        sizeof(RKNS_BOOL *) * biz_state->grpCount);
    if (!biz_state->chnEnabled) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for BUSINESS channel state\n");
        rkos_kfree(biz_state->grpEnabled);
        rkos_kfree(biz_state->chnCount);
        biz_state->grpEnabled = NULL;
        biz_state->chnCount = NULL;
        business_free_config(cfg);
        return RKNS_FAILURE;
    }
    rkos_memset(biz_state->chnEnabled, 0, sizeof(RKNS_BOOL *) * biz_state->grpCount);

    /* 为每个 group 创建 BUSINESS 组 */
    for (g = 0; g < (int)cfg->grpCount; g++) {
        BusinessGroupConfig *grp = &cfg->grpConfigs[g];
        ret = RKNS_BUSINESS_CreateGroup(grp->grpId, &grp->param);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RKNS_BUSINESS_CreateGroup failed for group %d: %d\n",
                    g, ret);
            /* 清理已创建的组 */
            for (int j = 0; j < g; j++) {
                RKNS_BUSINESS_DestroyGroup(cfg->grpConfigs[j].grpId);
            }
            /* 释放已分配的内存 */
            for (int j = 0; j < g; j++) {
                if (biz_state->chnEnabled[j]) {
                    rkos_kfree(biz_state->chnEnabled[j]);
                }
            }
            rkos_kfree(biz_state->chnEnabled);
            rkos_kfree(biz_state->chnCount);
            rkos_kfree(biz_state->grpEnabled);
            biz_state->chnEnabled = NULL;
            biz_state->chnCount = NULL;
            biz_state->grpEnabled = NULL;
            business_free_config(cfg);
            return ret;
        }
        biz_state->grpEnabled[g] = RKNS_TRUE;
        biz_state->chnCount[g] = grp->chnCount;

        /* 如果这个 group 有 channels，分配 channel enable 数组 */
        if (grp->chnCount > 0) {
            biz_state->chnEnabled[g] = (RKNS_BOOL *)rkos_kmalloc(
                sizeof(RKNS_BOOL) * grp->chnCount);
            if (!biz_state->chnEnabled[g]) {
                fprintf(stderr, "[ERROR] Failed to allocate memory for BUSINESS channel state\n");
                /* 清理已创建的组 */
                for (int j = 0; j <= g; j++) {
                    RKNS_BUSINESS_DestroyGroup(cfg->grpConfigs[j].grpId);
                }
                /* 释放已分配的内存 */
                for (int j = 0; j < g; j++) {
                    if (biz_state->chnEnabled[j]) {
                        rkos_kfree(biz_state->chnEnabled[j]);
                    }
                }
                rkos_kfree(biz_state->chnEnabled);
                rkos_kfree(biz_state->chnCount);
                rkos_kfree(biz_state->grpEnabled);
                biz_state->chnEnabled = NULL;
                biz_state->chnCount = NULL;
                biz_state->grpEnabled = NULL;
                business_free_config(cfg);
                return RKNS_FAILURE;
            }
            rkos_memset(biz_state->chnEnabled[g], 0, sizeof(RKNS_BOOL) * grp->chnCount);

            /* 暂时通道不需要创建，因为 BUSINESS 的 API 主要是基于 group 的 */
            for (c = 0; c < (int)grp->chnCount; c++) {
                biz_state->chnEnabled[g][c] = RKNS_TRUE;
            }
        }
    }

    state->chn.enModId = RKNS_ID_BUSINESS;
    state->chn.s32DevId = module->devId;
    if (cfg->grpCount > 0 && cfg->grpConfigs[0].chnCount > 0) {
        state->chn.s32ChnId = cfg->grpConfigs[0].chnConfigs[0].chnId;
    } else if (cfg->grpCount > 0) {
        state->chn.s32ChnId = cfg->grpConfigs[0].grpId;
    }
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 business_destroy(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    BusinessConfig *cfg = MODULE_CONFIG(module, BusinessConfig);
    RKNSLaunchBusinessState *biz_state = &state->bizState;

    if (state->created) {
        /* 销毁所有 BUSINESS 组 */
        for (int g = 0; g < (int)cfg->grpCount; g++) {
            if (biz_state->grpEnabled[g]) {
                RKNS_BUSINESS_DestroyGroup(cfg->grpConfigs[g].grpId);
                biz_state->grpEnabled[g] = RKNS_FALSE;
            }
        }
        /* 释放状态内存 */
        for (int g = 0; g < (int)biz_state->grpCount; g++) {
            if (biz_state->chnEnabled[g]) {
                rkos_kfree(biz_state->chnEnabled[g]);
            }
        }
        if (biz_state->chnEnabled) {
            rkos_kfree(biz_state->chnEnabled);
            biz_state->chnEnabled = NULL;
        }
        if (biz_state->chnCount) {
            rkos_kfree(biz_state->chnCount);
            biz_state->chnCount = NULL;
        }
        if (biz_state->grpEnabled) {
            rkos_kfree(biz_state->grpEnabled);
            biz_state->grpEnabled = NULL;
        }
        biz_state->grpCount = 0;
        state->created = RKNS_FALSE;
    }

    /* 释放配置内存 */
    business_free_config(cfg);

    return RKNS_SUCCESS;
}

RKNS_S32 business_start(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    BusinessConfig *cfg = MODULE_CONFIG(module, BusinessConfig);
    RKNSLaunchBusinessState *biz_state = &state->bizState;
    RKNS_S32 ret;

    for (int g = 0; g < (int)cfg->grpCount; g++) {
        if (biz_state->grpEnabled[g]) {
            ret = RKNS_BUSINESS_StartGroup(cfg->grpConfigs[g].grpId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] RKNS_BUSINESS_StartGroup failed for group %d: %d\n",
                        g, ret);
                return ret;
            }
        }
    }
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 business_stop(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    BusinessConfig *cfg = MODULE_CONFIG(module, BusinessConfig);
    RKNSLaunchBusinessState *biz_state = &state->bizState;
    RKNS_S32 ret;

    for (int g = 0; g < (int)cfg->grpCount; g++) {
        if (biz_state->grpEnabled[g]) {
            ret = RKNS_BUSINESS_StopGroup(cfg->grpConfigs[g].grpId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[WARNING] RKNS_BUSINESS_StopGroup failed for group %d: %d\n",
                        g, ret);
            }
        }
    }
    state->started = RKNS_FALSE;
    return RKNS_SUCCESS;
}

/* ---------- TEXT 模块 ---------- */

RKNS_S32 text_parse_config(YamlDocument *doc, const char *base_path,
                                          RKNSLaunchModule *module) {
    TextConfig *cfg = MODULE_CONFIG(module, TextConfig);
    char path[512], grp_base[512], chn_base[512];
    const char *str;
    int g, c, ret = RKNS_SUCCESS;
    int grp_count = 0, chn_count;

    rkos_memset(cfg, 0, sizeof(TextConfig));

    /* 先统计 group 数量 */
    snprintf(path, sizeof(path), "%s.groups", base_path);
    grp_count = yaml_get_count(doc, path);
    if (grp_count <= 0) grp_count = 1;

    /* 动态分配 group 配置数组 */
    cfg->grpCount = grp_count;
    cfg->grpConfigs = (TextGroupConfig *)rkos_kmalloc(
        sizeof(TextGroupConfig) * grp_count);
    if (!cfg->grpConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for TEXT group configs\n");
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->grpConfigs, 0, sizeof(TextGroupConfig) * grp_count);

    /* 解析每个 group */
    for (g = 0; g < grp_count; g++) {
        TextGroupConfig *grp = &cfg->grpConfigs[g];
        snprintf(grp_base, sizeof(grp_base), "%s.groups.%d", base_path, g);

        /* 获取 group ID */
        snprintf(path, sizeof(path), "%s.grp_id", grp_base);
        grp->grpId = (RKNS_S32)yaml_get_int(doc, path, g);

        /* 初始化 param */
        rkos_memset(&grp->param, 0, sizeof(RKNSTextParam));

        /* 解析 type - 这是 group 的属性 */
        snprintf(path, sizeof(path), "%s.grp_config.type", grp_base);
        str = yaml_get_string(doc, path, "CONTEXT");
        if (strcmp(str, "CONTEXT") == 0) {
            grp->param.type = RKNS_TEXT_TYPE_CONTEXT;
        } else if (strcmp(str, "AGENT") == 0) {
            grp->param.type = RKNS_TEXT_TYPE_AGENT;
        } else if (strcmp(str, "RAG") == 0) {
            grp->param.type = RKNS_TEXT_TYPE_RAG;
        } else if (strcmp(str, "MCP") == 0) {
            grp->param.type = RKNS_TEXT_TYPE_MCP;
        } else {
            grp->param.type = RKNS_TEXT_TYPE_MAX;
        }

        /* 解析各类型的参数 */
        if (grp->param.type == RKNS_TEXT_TYPE_CONTEXT) {
            snprintf(path, sizeof(path), "%s.grp_config.max_context_size", grp_base);
            grp->param.context.maxContextSize = yaml_get_int(doc, path, 4096);
            snprintf(path, sizeof(path), "%s.grp_config.system_prompt", grp_base);
            const char *system_prompt = yaml_get_string(doc, path, NULL);
            if (system_prompt) {
                grp->param.context.systemPrompt = (char *)rkos_kmalloc(strlen(system_prompt) + 1);
                if (grp->param.context.systemPrompt) {
                    strcpy(grp->param.context.systemPrompt, system_prompt);
                }
            }
        } else if (grp->param.type == RKNS_TEXT_TYPE_AGENT) {
            /* 暂不处理 AGENT 特定参数 */
        } else if (grp->param.type == RKNS_TEXT_TYPE_RAG) {
            /* 暂不处理 RAG 特定参数 */
        } else if (grp->param.type == RKNS_TEXT_TYPE_MCP) {
            /* 暂不处理 MCP 特定参数 */
        }

        /* 使用 yaml_get_count 获取该 group 的 channels 数量 */
        snprintf(path, sizeof(path), "%s.channels", grp_base);
        chn_count = yaml_get_count(doc, path);
        if (chn_count <= 0) {
            chn_count = 1;
        }

        grp->chnCount = chn_count;
        if (chn_count > 0) {
            /* 动态分配该 group 的 channel 配置数组 */
            grp->chnConfigs = (TextChannelConfig *)rkos_kmalloc(
                sizeof(TextChannelConfig) * chn_count);
            if (!grp->chnConfigs) {
                fprintf(stderr, "[ERROR] Failed to allocate memory for TEXT channel configs\n");
                /* 清理已分配的内存 */
                for (int j = 0; j <= g; j++) {
                    TextGroupConfig *temp_grp = &cfg->grpConfigs[j];
                    if (temp_grp->chnConfigs) {
                        rkos_kfree(temp_grp->chnConfigs);
                        temp_grp->chnConfigs = NULL;
                    }
                }
                rkos_kfree(cfg->grpConfigs);
                cfg->grpConfigs = NULL;
                cfg->grpCount = 0;
                return RKNS_FAILURE;
            }
            rkos_memset(grp->chnConfigs, 0, sizeof(TextChannelConfig) * chn_count);

            /* 解析该 group 的每个 channel */
            for (c = 0; c < chn_count; c++) {
                TextChannelConfig *chn = &grp->chnConfigs[c];
                snprintf(chn_base, sizeof(chn_base), "%s.channels.%d", grp_base, c);

                /* 获取 channel ID */
                snprintf(path, sizeof(path), "%s.chn_id", chn_base);
                chn->chnId = (RKNS_S32)yaml_get_int(doc, path, c);

                /* channel 暂时没有特殊的 chn_config 配置，就简单解析 */
            }
        }

        /* 打印 TEXT group 配置参数 */
        printf("[INFO] TEXT Group %d Config:\n", g);
        printf("[INFO]   grp_id: %d\n", grp->grpId);
        printf("[INFO]   type: ");
        if (grp->param.type == RKNS_TEXT_TYPE_CONTEXT) printf("CONTEXT\n");
        else if (grp->param.type == RKNS_TEXT_TYPE_AGENT) printf("AGENT\n");
        else if (grp->param.type == RKNS_TEXT_TYPE_RAG) printf("RAG\n");
        else if (grp->param.type == RKNS_TEXT_TYPE_MCP) printf("MCP\n");
        else printf("UNKNOWN\n");
        if (grp->param.type == RKNS_TEXT_TYPE_CONTEXT) {
            printf("[INFO]   max_context_size: %d\n", grp->param.context.maxContextSize);
            if (grp->param.context.systemPrompt) {
                printf("[INFO]   system_prompt: %s\n", grp->param.context.systemPrompt);
            }
        }
        printf("[INFO]   channel_count: %d\n", grp->chnCount);
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

static void text_free_config(TextConfig *cfg) {
    if (cfg->grpConfigs) {
        for (int j = 0; j < (int)cfg->grpCount; j++) {
            TextGroupConfig *grp = &cfg->grpConfigs[j];
            if (grp->param.type == RKNS_TEXT_TYPE_CONTEXT && grp->param.context.systemPrompt) {
                rkos_kfree(grp->param.context.systemPrompt);
                grp->param.context.systemPrompt = NULL;
            }
            if (grp->chnConfigs) {
                rkos_kfree(grp->chnConfigs);
                grp->chnConfigs = NULL;
            }
        }
        rkos_kfree(cfg->grpConfigs);
        cfg->grpConfigs = NULL;
    }
    cfg->grpCount = 0;
}

RKNS_S32 text_create(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    TextConfig *cfg = MODULE_CONFIG(module, TextConfig);
    RKNSLaunchTextState *text_state = &state->textState;
    int g, c, ret;

    rkos_memset(text_state, 0, sizeof(RKNSLaunchTextState));

    /* 初始化 group count */
    text_state->grpCount = cfg->grpCount;
    if (text_state->grpCount == 0) {
        fprintf(stderr, "[ERROR] No TEXT groups to create\n");
        text_free_config(cfg);
        return RKNS_FAILURE;
    }

    /* 动态分配 group enable 数组 */
    text_state->grpEnabled = (RKNS_BOOL *)rkos_kmalloc(
        sizeof(RKNS_BOOL) * text_state->grpCount);
    if (!text_state->grpEnabled) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for TEXT group state\n");
        text_free_config(cfg);
        return RKNS_FAILURE;
    }
    rkos_memset(text_state->grpEnabled, 0, sizeof(RKNS_BOOL) * text_state->grpCount);

    /* 动态分配 channel count 数组 */
    text_state->chnCount = (RKNS_U32 *)rkos_kmalloc(
        sizeof(RKNS_U32) * text_state->grpCount);
    if (!text_state->chnCount) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for TEXT channel count\n");
        rkos_kfree(text_state->grpEnabled);
        text_state->grpEnabled = NULL;
        text_free_config(cfg);
        return RKNS_FAILURE;
    }
    rkos_memset(text_state->chnCount, 0, sizeof(RKNS_U32) * text_state->grpCount);

    /* 动态分配 channel enable 二维数组 */
    text_state->chnEnabled = (RKNS_BOOL **)rkos_kmalloc(
        sizeof(RKNS_BOOL *) * text_state->grpCount);
    if (!text_state->chnEnabled) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for TEXT channel state\n");
        rkos_kfree(text_state->grpEnabled);
        rkos_kfree(text_state->chnCount);
        text_state->grpEnabled = NULL;
        text_state->chnCount = NULL;
        text_free_config(cfg);
        return RKNS_FAILURE;
    }
    rkos_memset(text_state->chnEnabled, 0, sizeof(RKNS_BOOL *) * text_state->grpCount);

    /* 为每个 group 创建 TEXT 组 */
    for (g = 0; g < (int)cfg->grpCount; g++) {
        TextGroupConfig *grp = &cfg->grpConfigs[g];
        ret = RKNS_TEXT_CreateGroup(grp->grpId, &grp->param);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RKNS_TEXT_CreateGroup failed for group %d: %d\n",
                    g, ret);
            /* 清理已创建的组 */
            for (int j = 0; j < g; j++) {
                RKNS_TEXT_DestroyGroup(cfg->grpConfigs[j].grpId);
            }
            /* 释放已分配的内存 */
            for (int j = 0; j < g; j++) {
                if (text_state->chnEnabled[j]) {
                    rkos_kfree(text_state->chnEnabled[j]);
                }
            }
            rkos_kfree(text_state->chnEnabled);
            rkos_kfree(text_state->chnCount);
            rkos_kfree(text_state->grpEnabled);
            text_state->chnEnabled = NULL;
            text_state->chnCount = NULL;
            text_state->grpEnabled = NULL;
            text_free_config(cfg);
            return ret;
        }
        text_state->grpEnabled[g] = RKNS_TRUE;
        text_state->chnCount[g] = grp->chnCount;

        /* 如果这个 group 有 channels，分配 channel enable 数组 */
        if (grp->chnCount > 0) {
            text_state->chnEnabled[g] = (RKNS_BOOL *)rkos_kmalloc(
                sizeof(RKNS_BOOL) * grp->chnCount);
            if (!text_state->chnEnabled[g]) {
                fprintf(stderr, "[ERROR] Failed to allocate memory for TEXT channel state\n");
                /* 清理已创建的组 */
                for (int j = 0; j <= g; j++) {
                    RKNS_TEXT_DestroyGroup(cfg->grpConfigs[j].grpId);
                }
                /* 释放已分配的内存 */
                for (int j = 0; j < g; j++) {
                    if (text_state->chnEnabled[j]) {
                        rkos_kfree(text_state->chnEnabled[j]);
                    }
                }
                rkos_kfree(text_state->chnEnabled);
                rkos_kfree(text_state->chnCount);
                rkos_kfree(text_state->grpEnabled);
                text_state->chnEnabled = NULL;
                text_state->chnCount = NULL;
                text_state->grpEnabled = NULL;
                text_free_config(cfg);
                return RKNS_FAILURE;
            }
            rkos_memset(text_state->chnEnabled[g], 0, sizeof(RKNS_BOOL) * grp->chnCount);

            /* 暂时通道不需要创建，因为 TEXT 的 API 主要是基于 group 的 */
            for (c = 0; c < (int)grp->chnCount; c++) {
                text_state->chnEnabled[g][c] = RKNS_TRUE;
            }
        }
    }

    state->chn.enModId = RKNS_ID_TEXT;
    state->chn.s32DevId = module->devId;
    if (cfg->grpCount > 0 && cfg->grpConfigs[0].chnCount > 0) {
        state->chn.s32ChnId = cfg->grpConfigs[0].chnConfigs[0].chnId;
    } else if (cfg->grpCount > 0) {
        state->chn.s32ChnId = cfg->grpConfigs[0].grpId;
    }
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 text_destroy(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    TextConfig *cfg = MODULE_CONFIG(module, TextConfig);
    RKNSLaunchTextState *text_state = &state->textState;

    if (state->created) {
        /* 销毁所有 TEXT 组 */
        for (int g = 0; g < (int)cfg->grpCount; g++) {
            if (text_state->grpEnabled[g]) {
                RKNS_TEXT_DestroyGroup(cfg->grpConfigs[g].grpId);
                text_state->grpEnabled[g] = RKNS_FALSE;
            }
        }
        /* 释放状态内存 */
        for (int g = 0; g < (int)text_state->grpCount; g++) {
            if (text_state->chnEnabled[g]) {
                rkos_kfree(text_state->chnEnabled[g]);
            }
        }
        if (text_state->chnEnabled) {
            rkos_kfree(text_state->chnEnabled);
            text_state->chnEnabled = NULL;
        }
        if (text_state->chnCount) {
            rkos_kfree(text_state->chnCount);
            text_state->chnCount = NULL;
        }
        if (text_state->grpEnabled) {
            rkos_kfree(text_state->grpEnabled);
            text_state->grpEnabled = NULL;
        }
        text_state->grpCount = 0;
        state->created = RKNS_FALSE;
    }

    /* 释放配置内存 */
    text_free_config(cfg);

    return RKNS_SUCCESS;
}

RKNS_S32 text_start(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    TextConfig *cfg = MODULE_CONFIG(module, TextConfig);
    RKNSLaunchTextState *text_state = &state->textState;
    RKNS_S32 ret;

    for (int g = 0; g < (int)cfg->grpCount; g++) {
        if (text_state->grpEnabled[g]) {
            ret = RKNS_TEXT_StartGroup(cfg->grpConfigs[g].grpId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] RKNS_TEXT_StartGroup failed for group %d: %d\n",
                        g, ret);
                return ret;
            }
        }
    }
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 text_stop(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    TextConfig *cfg = MODULE_CONFIG(module, TextConfig);
    RKNSLaunchTextState *text_state = &state->textState;
    RKNS_S32 ret;

    for (int g = 0; g < (int)cfg->grpCount; g++) {
        if (text_state->grpEnabled[g]) {
            ret = RKNS_TEXT_StopGroup(cfg->grpConfigs[g].grpId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[WARNING] RKNS_TEXT_StopGroup failed for group %d: %d\n",
                        g, ret);
            }
        }
    }
    state->started = RKNS_FALSE;
    return RKNS_SUCCESS;
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
    char path[512];

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

/**
 * @brief DETECT 模型属性解析（YoloV8/YoloX 等）
 * YAML 字段：model_name, weight_path, rknn_version, core_mask
 */
static RKNS_S32 detect_parse_model_config(YamlDocument *doc, const char *base_path,
                                          RKNS_INFER_ATTR_S *attr,
                                          char *str_pool, size_t pool_size,
                                          size_t *offset) {
    RKNS_DETECT_MODEL_ATTR_S *m = &attr->stDetectAttr;
    char path[512];

    snprintf(path, sizeof(path), "%s.model_name", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pModelName);

    snprintf(path, sizeof(path), "%s.weight_path", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pWeightPath);

    snprintf(path, sizeof(path), "%s.rknn_version", base_path);
    m->u32RknnVersion = (RKNS_U32)yaml_get_int(doc, path, 0);

    snprintf(path, sizeof(path), "%s.core_mask", base_path);
    m->u32CoreMask = (RKNS_U32)yaml_get_int(doc, path, 0);

    return RKNS_SUCCESS;
}

/**
 * @brief LLM 模型属性解析
 * YAML 字段：llm_model_name, llm_device_num, llm_top_k, llm_top_p, 
 *            llm_temperature, llm_max_new_tokens, llm_max_context_len,
 *            llm_system_prompt, llm_prompt_prefix, llm_prompt_postfix
 */
static RKNS_S32 llm_parse_model_config(YamlDocument *doc, const char *base_path,
                                       RKNS_INFER_ATTR_S *attr,
                                       char *str_pool, size_t pool_size,
                                       size_t *offset) {
    RKNS_LLM_MODEL_ATTR_S *m = &attr->stLlmAttr;
    char path[512];

    snprintf(path, sizeof(path), "%s.llm_model_name", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pModelName);

    snprintf(path, sizeof(path), "%s.llm_device_num", base_path);
    m->s32DeviceNum = (RKNS_S32)yaml_get_int(doc, path, 0);

    snprintf(path, sizeof(path), "%s.llm_top_k", base_path);
    m->s32TopK = (RKNS_S32)yaml_get_int(doc, path, 1);

    snprintf(path, sizeof(path), "%s.llm_top_p", base_path);
    m->fTopP = (RKNS_FLOAT)yaml_get_float(doc, path, 0.9);

    snprintf(path, sizeof(path), "%s.llm_temperature", base_path);
    m->fTemperature = (RKNS_FLOAT)yaml_get_float(doc, path, 1.0);

    snprintf(path, sizeof(path), "%s.llm_max_new_tokens", base_path);
    m->s32MaxNewTokens = (RKNS_S32)yaml_get_int(doc, path, 256);

    snprintf(path, sizeof(path), "%s.llm_max_context_len", base_path);
    m->s32MaxContextLen = (RKNS_S32)yaml_get_int(doc, path, 2048);

    snprintf(path, sizeof(path), "%s.llm_system_prompt", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pSystemPrompt);

    snprintf(path, sizeof(path), "%s.llm_prompt_prefix", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pPromptPrefix);

    snprintf(path, sizeof(path), "%s.llm_prompt_postfix", base_path);
    str_pool_alloc(doc, path, str_pool, pool_size, offset, &m->pPromptPostfix);

    return RKNS_SUCCESS;
}

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
    { RKNS_MODEL_TYPE_DETECT,   detect_parse_model_config },
    { RKNS_MODEL_TYPE_LLM,      llm_parse_model_config },
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
    InferConfig *cfg = MODULE_CONFIG(module, InferConfig);
    char path[512];
    int chn_count;
    int ci;

    rkos_memset(cfg, 0, sizeof(InferConfig));

    /* ========== 获取通道数量并分配内存 ========== */
    snprintf(path, sizeof(path), "%s.channels", base_path);
    chn_count = yaml_get_count(doc, path);
    if (chn_count <= 0) chn_count = 1;

    cfg->chnCount = (RKNS_U32)chn_count;
    cfg->chnConfigs = (InferChannelConfig*)rkos_kmalloc(sizeof(InferChannelConfig) * chn_count);
    if (!cfg->chnConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for %d INFER channels\n", chn_count);
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->chnConfigs, 0, sizeof(InferChannelConfig) * chn_count);

    /* ========== 解析每个通道 ========== */
    for (ci = 0; ci < chn_count; ci++) {
        InferChannelConfig *chn = &cfg->chnConfigs[ci];
        char chn_base[512];
        snprintf(chn_base, sizeof(chn_base), "%s.channels.%d", base_path, ci);

        /* 获取通道 ID */
        snprintf(path, sizeof(path), "%s.chn_id", chn_base);
        chn->chnId = (RKNS_S32)yaml_get_int(doc, path, ci);

        /* 为每个通道分配独立的字符串池 */
        chn->strPoolSize = RKNS_MODULE_CONFIG_SIZE / 2;
        chn->strPool = (char*)rkos_kmalloc(chn->strPoolSize);
        if (!chn->strPool) {
            fprintf(stderr, "[ERROR] Failed to allocate string pool for INFER channel %d\n", ci);
            /* 释放已分配的资源 */
            for (int j = 0; j <= ci; j++) {
                if (cfg->chnConfigs[j].strPool) {
                    rkos_kfree(cfg->chnConfigs[j].strPool);
                }
            }
            rkos_kfree(cfg->chnConfigs);
            cfg->chnConfigs = NULL;
            cfg->chnCount = 0;
            return RKNS_FAILURE;
        }
        rkos_memset(chn->strPool, 0, chn->strPoolSize);
        size_t offset = 0;

        RKNS_INFER_ATTR_S *attr = &chn->attr;

        /* 解析模型类型 */
        snprintf(path, sizeof(path), "%s.chn_config.model_type", chn_base);
        const char *model_type_str = yaml_get_string(doc, path, "DETECT");
        attr->enModelType = parse_model_type(model_type_str);

        /* 解析公共参数 */
        snprintf(path, sizeof(path), "%s.chn_config.input_queue_count", chn_base);
        attr->u32InputQueueCnt = (RKNS_U32)yaml_get_int(doc, path, 3);

        snprintf(path, sizeof(path), "%s.chn_config.output_buffer_count", chn_base);
        attr->u32BufferCnt = (RKNS_U32)yaml_get_int(doc, path, 3);

        snprintf(path, sizeof(path), "%s.chn_config.depth", chn_base);
        attr->depth = (RKNS_U32)yaml_get_int(doc, path, 0);

        snprintf(path, sizeof(path), "%s.chn_config.src_frame_rate", chn_base);
        attr->s32SrcFrameRate = (RKNS_S32)yaml_get_int(doc, path, 0);

        snprintf(path, sizeof(path), "%s.chn_config.dst_frame_rate", chn_base);
        attr->s32DstFrameRate = (RKNS_S32)yaml_get_int(doc, path, 0);

        /* 解析 stModelAttr 的字符串属性 */
        snprintf(path, sizeof(path), "%s.chn_config.module_name", chn_base);
        str_pool_alloc(doc, path, chn->strPool, chn->strPoolSize, &offset,
                       &attr->stModelAttr.pModuleName);

        snprintf(path, sizeof(path), "%s.chn_config.model_path", chn_base);
        str_pool_alloc(doc, path, chn->strPool, chn->strPoolSize, &offset,
                       &attr->stModelAttr.pDataPath);

        snprintf(path, sizeof(path), "%s.chn_config.model_auth_config", chn_base);
        str_pool_alloc(doc, path, chn->strPool, chn->strPoolSize, &offset,
                       &attr->stModelAttr.pAuthConfig);

        /* 解析模型特有属性 */
        ModelConfigParseFunc model_parse = find_model_config_parser(attr->enModelType);
        if (model_parse) {
            char chn_config_path[512];
            snprintf(chn_config_path, sizeof(chn_config_path), "%s.chn_config", chn_base);
            model_parse(doc, chn_config_path, attr, chn->strPool, chn->strPoolSize, &offset);
        }
    }

    /* ========== 打印配置 ========== */
    printf("[INFO] INFER Module Config:\n");
    printf("[INFO]   Total channels: %u\n", cfg->chnCount);
    for (ci = 0; ci < (int)cfg->chnCount; ci++) {
        InferChannelConfig *chn = &cfg->chnConfigs[ci];
        RKNS_INFER_ATTR_S *attr = &chn->attr;
        printf("[INFO]   Channel[%d]: chn_id=%d, model_type=%s, module_name=%s\n",
               ci, chn->chnId, g_modelTypeStr[attr->enModelType],
               attr->stModelAttr.pModuleName ? attr->stModelAttr.pModuleName : "(null)");
        printf("[INFO]     input_queue_count: %u, output_buffer_count: %u, depth: %u\n",
               attr->u32InputQueueCnt, attr->u32BufferCnt, attr->depth);
        
        if (attr->enModelType == RKNS_MODEL_TYPE_ALM) {
            printf("[INFO]     ALM Model: task=%s, encoder=%s, decoder=%s\n",
                   attr->stAlmAttr.pTask ? attr->stAlmAttr.pTask : "(null)",
                   attr->stAlmAttr.pEncoderModelName ? attr->stAlmAttr.pEncoderModelName : "(null)",
                   attr->stAlmAttr.pDecoderModelName ? attr->stAlmAttr.pDecoderModelName : "(null)");
        } else if (attr->enModelType == RKNS_MODEL_TYPE_DETECT) {
            printf("[INFO]     DETECT Model: model_name=%s, rknn_ver=%u, core_mask=0x%x\n",
                   attr->stDetectAttr.pModelName ? attr->stDetectAttr.pModelName : "(null)",
                   attr->stDetectAttr.u32RknnVersion,
                   attr->stDetectAttr.u32CoreMask);
        } else if (attr->enModelType == RKNS_MODEL_TYPE_LLM) {
            printf("[INFO]     LLM Model: model_name=%s, device_num=%d, temp=%.2f\n",
                   attr->stLlmAttr.pModelName ? attr->stLlmAttr.pModelName : "(null)",
                   attr->stLlmAttr.s32DeviceNum,
                   attr->stLlmAttr.fTemperature);
        }
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

static void infer_free_config(InferConfig *cfg) {
    if (cfg->chnConfigs != NULL) {
        for (int ci = 0; ci < (int)cfg->chnCount; ci++) {
            if (cfg->chnConfigs[ci].strPool != NULL) {
                rkos_kfree(cfg->chnConfigs[ci].strPool);
            }
        }
        rkos_kfree(cfg->chnConfigs);
        cfg->chnConfigs = NULL;
        cfg->chnCount = 0;
    }
}

RKNS_S32 infer_create(const RKNSLaunchModule *module,
                              RKNSLaunchModuleState *state) {
    const InferConfig *cfg = MODULE_CONFIG(module, InferConfig);
    RKNS_S32 ret;
    int ci;

    rkos_memset(&state->inferState, 0, sizeof(state->inferState));
    state->inferState.chnCount = cfg->chnCount;
    
    /* 动态分配状态数组 */
    if (cfg->chnCount > 0) {
        state->inferState.chnEnabled = (RKNS_BOOL*)rkos_kmalloc(sizeof(RKNS_BOOL) * cfg->chnCount);
        if (!state->inferState.chnEnabled) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for INFER channel states\n");
            infer_free_config((InferConfig*)cfg);
            return RKNS_FAILURE;
        }
        rkos_memset(state->inferState.chnEnabled, 0, sizeof(RKNS_BOOL) * cfg->chnCount);
    }

    for (ci = 0; ci < (int)cfg->chnCount; ci++) {
        const InferChannelConfig *chn = &cfg->chnConfigs[ci];
        
        /* 创建 INFER 通道 */
        ret = RKNS_INFER_CreateChn((INFER_CHN)chn->chnId, &chn->attr);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RKNS_INFER_CreateChn failed for channel %d: %d\n", ci, ret);
            /* 销毁已创建的通道 */
            for (int j = 0; j < ci; j++) {
                if (state->inferState.chnEnabled[j]) {
                    RKNS_INFER_DestroyChn((INFER_CHN)cfg->chnConfigs[j].chnId);
                    state->inferState.chnEnabled[j] = RKNS_FALSE;
                }
            }
            /* 释放状态数组 */
            if (state->inferState.chnEnabled) {
                rkos_kfree(state->inferState.chnEnabled);
                state->inferState.chnEnabled = NULL;
            }
            state->inferState.chnCount = 0;
            /* 释放配置内存 */
            infer_free_config((InferConfig*)cfg);
            return ret;
        }
        
        state->inferState.chnEnabled[ci] = RKNS_TRUE;
        printf("[INFO] INFER channel %d created (chn_id=%d)\n", ci, chn->chnId);
    }

    /* 设置默认通道信息（保持向后兼容） */
    if (cfg->chnCount > 0) {
        state->chn.enModId = RKNS_ID_INFER;
        state->chn.s32DevId = module->devId;
        state->chn.s32ChnId = cfg->chnConfigs[0].chnId;
    }
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 infer_destroy(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    const InferConfig *cfg = MODULE_CONFIG(module, InferConfig);
    
    if (state->created) {
        /* 销毁所有 INFER 通道 */
        for (int ci = (int)cfg->chnCount - 1; ci >= 0; ci--) {
            if (state->inferState.chnEnabled && state->inferState.chnEnabled[ci]) {
                RKNS_INFER_DestroyChn((INFER_CHN)cfg->chnConfigs[ci].chnId);
                state->inferState.chnEnabled[ci] = RKNS_FALSE;
                printf("[INFO] INFER channel %d destroyed (chn_id=%d)\n", ci, cfg->chnConfigs[ci].chnId);
            }
        }
        
        /* 释放状态数组 */
        if (state->inferState.chnEnabled != NULL) {
            rkos_kfree(state->inferState.chnEnabled);
            state->inferState.chnEnabled = NULL;
        }
        
        /* 释放配置内存 */
        infer_free_config((InferConfig*)cfg);
        
        rkos_memset(&state->inferState, 0, sizeof(state->inferState));
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 infer_start(const RKNSLaunchModule *module,
                             RKNSLaunchModuleState *state) {
    const InferConfig *cfg = MODULE_CONFIG(module, InferConfig);
    RKNS_S32 ret;
    
    for (int ci = 0; ci < (int)cfg->chnCount; ci++) {
        if (state->inferState.chnEnabled && state->inferState.chnEnabled[ci]) {
            ret = RKNS_INFER_StartChn((INFER_CHN)cfg->chnConfigs[ci].chnId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] RKNS_INFER_StartChn failed for channel %d: %d\n", ci, ret);
                return ret;
            }
            printf("[INFO] INFER channel %d started (chn_id=%d)\n", ci, cfg->chnConfigs[ci].chnId);
        }
    }
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 infer_stop(const RKNSLaunchModule *module,
                            RKNSLaunchModuleState *state) {
    const InferConfig *cfg = MODULE_CONFIG(module, InferConfig);
    RKNS_S32 ret;
    
    for (int ci = (int)cfg->chnCount - 1; ci >= 0; ci--) {
        if (state->inferState.chnEnabled && state->inferState.chnEnabled[ci]) {
            ret = RKNS_INFER_StopChn((INFER_CHN)cfg->chnConfigs[ci].chnId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] RKNS_INFER_StopChn failed for channel %d: %d\n", ci, ret);
                /* 继续停止其他通道 */
            } else {
                printf("[INFO] INFER channel %d stopped (chn_id=%d)\n", ci, cfg->chnConfigs[ci].chnId);
            }
        }
    }
    state->started = RKNS_FALSE;
    return RKNS_SUCCESS;
}

/* ---------- GRAPHICS 模块 ---------- */

RKNS_S32 graphics_parse_config(YamlDocument *doc, const char *base_path,
                                       RKNSLaunchModule *module) {
    RKNSGraphicsParam *param = MODULE_CONFIG(module, RKNSGraphicsParam);
    RKNS_OVERLAY_RULE_S *rule = MODULE_SECONDARY(module, RKNS_OVERLAY_RULE_S);
    char path[256];
    char chn_path[256];
    const char *str;
    int rule_count;

    rkos_memset(param, 0, sizeof(RKNSGraphicsParam));
    rkos_memset(rule, 0, sizeof(RKNS_OVERLAY_RULE_S));

    /* 实际配置在 channels.0.chn_config 下 */
    snprintf(chn_path, sizeof(chn_path), "%s.channels.0.chn_config", base_path);

    /* 获取通道 ID */
    snprintf(path, sizeof(path), "%s.channels.0.chn_id", base_path);
    module->chnId = (RKNS_S32)yaml_get_int(doc, path, 0);

    /* 主配置: RKNSGraphicsParam */
    param->type = RKNS_GRAPHICS_TYPE_OVERLAY;

    snprintf(path, sizeof(path), "%s.width", chn_path);
    param->attr.width = (RKNS_U32)yaml_get_int(doc, path, 640);

    snprintf(path, sizeof(path), "%s.height", chn_path);
    param->attr.height = (RKNS_U32)yaml_get_int(doc, path, 480);

    param->attr.stride = param->attr.width;

    snprintf(path, sizeof(path), "%s.pixel_format", chn_path);
    str = yaml_get_string(doc, path, "NV12");
    param->attr.pixelFmt = parse_pixel_format(str);

    /* 背景色 */
    snprintf(path, sizeof(path), "%s.bg_color.r", chn_path);
    param->attr.bgColor.r = (RKNS_U8)yaml_get_int(doc, path, 0);
    snprintf(path, sizeof(path), "%s.bg_color.g", chn_path);
    param->attr.bgColor.g = (RKNS_U8)yaml_get_int(doc, path, 0);
    snprintf(path, sizeof(path), "%s.bg_color.b", chn_path);
    param->attr.bgColor.b = (RKNS_U8)yaml_get_int(doc, path, 0);
    snprintf(path, sizeof(path), "%s.bg_color.a", chn_path);
    param->attr.bgColor.a = (RKNS_U8)yaml_get_int(doc, path, 0x00);

    snprintf(path, sizeof(path), "%s.max_box_per_frame", chn_path);
    param->attr.maxBoxPerFrame = (RKNS_U32)yaml_get_int(doc, path, 32);

    snprintf(path, sizeof(path), "%s.line_width", chn_path);
    param->attr.lineWidth = (RKNS_U32)yaml_get_int(doc, path, 2);

    snprintf(path, sizeof(path), "%s.text_scale", chn_path);
    param->attr.textScale = (RKNS_U32)yaml_get_int(doc, path, 1);

    /* 次级配置: RKNS_OVERLAY_RULE_S */
    snprintf(path, sizeof(path), "%s.enable_box", chn_path);
    rule->enableBox = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

    snprintf(path, sizeof(path), "%s.enable_text", chn_path);
    rule->enableText = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

    snprintf(path, sizeof(path), "%s.global_score_threshold", chn_path);
    rule->globalScoreThreshold = (RKNS_FLOAT)yaml_get_float(doc, path, 0.4);

    snprintf(path, sizeof(path), "%s.text_scale", chn_path);
    rule->textScale = (RKNS_U32)yaml_get_int(doc, path, 1);

    /* 解析 overlay rules 子列表 */
    snprintf(path, sizeof(path), "%s.rules", chn_path);
    rule_count = yaml_get_count(doc, path);
    if (rule_count > 0 && rule_count <= RKNS_OVERLAY_MAX_CLS_NUM) {
        int ri;
        rule->clsNum = (RKNS_U32)rule_count;
        for (ri = 0; ri < rule_count; ri++) {
            RKNS_OVERLAY_RULE_ITEM_S *item = &rule->clsRule[ri];

            snprintf(path, sizeof(path), "%s.rules.%d.enable", chn_path, ri);
            item->enable = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

            snprintf(path, sizeof(path), "%s.rules.%d.score_threshold", chn_path, ri);
            item->scoreThreshold = (RKNS_FLOAT)yaml_get_float(doc, path, 0.5);

            snprintf(path, sizeof(path), "%s.rules.%d.color.r", chn_path, ri);
            item->color.r = (RKNS_U8)yaml_get_int(doc, path, 255);

            snprintf(path, sizeof(path), "%s.rules.%d.color.g", chn_path, ri);
            item->color.g = (RKNS_U8)yaml_get_int(doc, path, 0);

            snprintf(path, sizeof(path), "%s.rules.%d.color.b", chn_path, ri);
            item->color.b = (RKNS_U8)yaml_get_int(doc, path, 0);

            snprintf(path, sizeof(path), "%s.rules.%d.color.a", chn_path, ri);
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
           param->attr.pixelFmt == ROCKX_PIXEL_FORMAT_YUV420SP_NV12 ? "NV12" :
           param->attr.pixelFmt == ROCKX_PIXEL_FORMAT_RGB888 ? "RGB888" : "ARGB8888");
    printf("[INFO]   bg_color: A=%u, R=%u, G=%u, B=%u\n",
           param->attr.bgColor.a, param->attr.bgColor.r,
           param->attr.bgColor.g, param->attr.bgColor.b);
    printf("[INFO]   max_box_per_frame: %u\n", param->attr.maxBoxPerFrame);
    printf("[INFO]   line_width: %u\n", param->attr.lineWidth);
    printf("[INFO]   text_scale: %u (attr)\n", param->attr.textScale);
    printf("[INFO]   enable_box: %s\n", rule->enableBox ? "true" : "false");
    printf("[INFO]   enable_text: %s\n", rule->enableText ? "true" : "false");
    printf("[INFO]   global_score_threshold: %.2f\n", rule->globalScoreThreshold);
    printf("[INFO]   text_scale: %u (rule)\n", rule->textScale);
    printf("[INFO]   rules_count: %u\n", rule->clsNum);
    if (rule->clsNum > 0) {
        RKNS_U32 ri;
        for (ri = 0; ri < rule->clsNum; ri++) {
            const RKNS_OVERLAY_RULE_ITEM_S *item = &rule->clsRule[ri];
            printf("[INFO]   rules[%u]:\n", ri);
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

/**
 * @brief 辅助函数：将字符串从 YAML 复制到字符串池，并设置指针
 */
static RKNS_S32 output_str_pool_alloc(YamlDocument *doc, const char *path,
                                char *strPool, size_t poolSize,
                                size_t *offset, char **pptr) {
    const char *str = yaml_get_string(doc, path, NULL);
    if (!str) {
        *pptr = NULL;
        return RKNS_SUCCESS;
    }

    size_t len = strlen(str) + 1;
    if (*offset + len > poolSize) {
        fprintf(stderr, "[ERROR] String pool overflow for %s\n", path);
        return RKNS_FAILURE;
    }

    memcpy(strPool + *offset, str, len);
    *pptr = strPool + *offset;
    *offset += len;
    return RKNS_SUCCESS;
}

RKNS_S32 output_parse_config(YamlDocument *doc, const char *base_path,
                                      RKNSLaunchModule *module) {
    OutputConfig *cfg = MODULE_CONFIG(module, OutputConfig);
    char path[512];
    int chn_count;
    int ci;

    rkos_memset(cfg, 0, sizeof(OutputConfig));

    /* ========== 获取通道数量并分配内存 ========== */
    snprintf(path, sizeof(path), "%s.channels", base_path);
    chn_count = yaml_get_count(doc, path);
    if (chn_count <= 0) chn_count = 1;

    cfg->chnCount = (RKNS_U32)chn_count;
    cfg->chnConfigs = (OutputChannelConfig *)rkos_kmalloc(sizeof(OutputChannelConfig) * chn_count);
    if (!cfg->chnConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for %d OUTPUT channels\n", chn_count);
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->chnConfigs, 0, sizeof(OutputChannelConfig) * chn_count);

    /* ========== 解析每个通道 ========== */
    for (ci = 0; ci < chn_count; ci++) {
        OutputChannelConfig *chn = &cfg->chnConfigs[ci];
        char chn_base[512];
        snprintf(chn_base, sizeof(chn_base), "%s.channels.%d", base_path, ci);

        /* 获取通道 ID */
        snprintf(path, sizeof(path), "%s.chn_id", chn_base);
        chn->chnId = (RKNS_S32)yaml_get_int(doc, path, ci);

        /* 为每个通道分配独立的字符串池 */
        chn->strPoolSize = RKNS_MODULE_CONFIG_SIZE / 2;
        chn->strPool = (char *)rkos_kmalloc(chn->strPoolSize);
        if (!chn->strPool) {
            fprintf(stderr, "[ERROR] Failed to allocate string pool for OUTPUT channel %d\n", ci);
            /* 释放已分配的资源 */
            for (int j = 0; j <= ci; j++) {
                if (cfg->chnConfigs[j].strPool) {
                    rkos_kfree(cfg->chnConfigs[j].strPool);
                }
            }
            rkos_kfree(cfg->chnConfigs);
            cfg->chnConfigs = NULL;
            cfg->chnCount = 0;
            return RKNS_FAILURE;
        }
        rkos_memset(chn->strPool, 0, chn->strPoolSize);
        size_t offset = 0;

        RKNSOutputParam *param = &chn->param;

        /* 解析通用参数 */
        snprintf(path, sizeof(path), "%s.chn_config.type", chn_base);
        const char *type_str = yaml_get_string(doc, path, "APP");
        param->type = parse_output_type(type_str);

        snprintf(path, sizeof(path), "%s.chn_config.watch_depth", chn_base);
        param->watchDepth = (int)yaml_get_int(doc, path, 1);

        /* 解析操作类型 */
        snprintf(path, sizeof(path), "%s.chn_config.op_type", chn_base);
        const char *op_str = yaml_get_string(doc, path, "PRINT");
        if (param->type == RKNS_OUTPUT_TYPE_APP) {
            if (strcmp(op_str, "SAVE_FILE") == 0) {
                param->app.cmd = RKNS_APP_CMD_SAVE_FILE;
                /* 可选：解析 filePath */
                snprintf(path, sizeof(path), "%s.chn_config.file_path", chn_base);
                output_str_pool_alloc(doc, path, chn->strPool, chn->strPoolSize, &offset,
                                     &param->app.saveFile.filePath);
            } else {
                param->app.cmd = RKNS_APP_CMD_PRINT;
            }
        } else if (param->type == RKNS_OUTPUT_TYPE_ROCKIT) {
            if (strcmp(op_str, "UPDATE_RGN") == 0) {
                param->rockit.cmd = RKNS_ROCKIT_CMD_UPDATE_RGN;
                snprintf(path, sizeof(path), "%s.chn_config.rgn_id", chn_base);
                param->rockit.updateRgn.rgnId = yaml_get_int(doc, path, 0);
            } else if (strcmp(op_str, "SEND_VPSS") == 0) {
                param->rockit.cmd = RKNS_ROCKIT_CMD_SEND_VPSS;
                snprintf(path, sizeof(path), "%s.chn_config.vpss_group_id", chn_base);
                param->rockit.sendVpss.vpssGroupId = yaml_get_int(doc, path, 0);
                snprintf(path, sizeof(path), "%s.chn_config.vpss_chn_id", chn_base);
                param->rockit.sendVpss.vpssChnId = yaml_get_int(doc, path, 0);
            } else if (strcmp(op_str, "SEND_VO") == 0) {
                param->rockit.cmd = RKNS_ROCKIT_CMD_SEND_VO;
                snprintf(path, sizeof(path), "%s.chn_config.vo_layer_id", chn_base);
                param->rockit.sendVo.volayerId = yaml_get_int(doc, path, 0);
                snprintf(path, sizeof(path), "%s.chn_config.vo_chn_id", chn_base);
                param->rockit.sendVo.voChnId = yaml_get_int(doc, path, 0);
            } else if (strcmp(op_str, "SEND_AO") == 0) {
                param->rockit.cmd = RKNS_ROCKIT_CMD_SEND_AO;
                snprintf(path, sizeof(path), "%s.chn_config.ao_chn_id", chn_base);
                param->rockit.sendAo.aoChnId = yaml_get_int(doc, path, 0);
            } else if (strcmp(op_str, "SEND_AENC") == 0) {
                param->rockit.cmd = RKNS_ROCKIT_CMD_SEND_AENC;
                snprintf(path, sizeof(path), "%s.chn_config.aenc_chn_id", chn_base);
                param->rockit.sendAenc.aencChnId = yaml_get_int(doc, path, 0);
            } else if (strcmp(op_str, "SEND_VENC") == 0) {
                param->rockit.cmd = RKNS_ROCKIT_CMD_SEND_VENC;
                snprintf(path, sizeof(path), "%s.chn_config.venc_chn_id", chn_base);
                param->rockit.sendVenc.vencChnId = yaml_get_int(doc, path, 0);
            } else {
                param->rockit.cmd = RKNS_ROCKIT_CMD_SEND_VO;
                param->rockit.sendVo.volayerId = 0;
                param->rockit.sendVo.voChnId = 0;
            }
        } else if (param->type == RKNS_OUTPUT_TYPE_GSTREAM) {
            snprintf(path, sizeof(path), "%s.chn_config.pipeline", chn_base);
            output_str_pool_alloc(doc, path, chn->strPool, chn->strPoolSize, &offset,
                                 &param->gstream.pipeline);
            snprintf(path, sizeof(path), "%s.chn_config.timeout", chn_base);
            param->gstream.timeout = yaml_get_int(doc, path, 5000);
        } else if (param->type == RKNS_OUTPUT_TYPE_OPENCV) {
            snprintf(path, sizeof(path), "%s.chn_config.window_name", chn_base);
            output_str_pool_alloc(doc, path, chn->strPool, chn->strPoolSize, &offset,
                                 &param->opencv.windowName);
            snprintf(path, sizeof(path), "%s.chn_config.width", chn_base);
            param->opencv.width = yaml_get_int(doc, path, 640);
            snprintf(path, sizeof(path), "%s.chn_config.height", chn_base);
            param->opencv.height = yaml_get_int(doc, path, 480);
        }
    }

    /* ========== 打印配置 ========== */
    printf("[INFO] OUTPUT Module Config:\n");
    printf("[INFO]   Total channels: %u\n", cfg->chnCount);
    for (ci = 0; ci < (int)cfg->chnCount; ci++) {
        OutputChannelConfig *chn = &cfg->chnConfigs[ci];
        RKNSOutputParam *param = &chn->param;
        printf("[INFO]   Channel[%d]: chn_id=%d, output_type=%s\n",
               ci, chn->chnId,
               param->type == RKNS_OUTPUT_TYPE_APP ? "APP" :
               param->type == RKNS_OUTPUT_TYPE_ROCKIT ? "ROCKIT" :
               param->type == RKNS_OUTPUT_TYPE_GSTREAM ? "GSTREAM" : "OPENCV");
        printf("[INFO]     watch_depth: %d\n", param->watchDepth);
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

static void output_free_config(OutputConfig *cfg) {
    if (cfg->chnConfigs != NULL) {
        for (int ci = 0; ci < (int)cfg->chnCount; ci++) {
            if (cfg->chnConfigs[ci].strPool != NULL) {
                rkos_kfree(cfg->chnConfigs[ci].strPool);
            }
        }
        rkos_kfree(cfg->chnConfigs);
        cfg->chnConfigs = NULL;
        cfg->chnCount = 0;
    }
}

RKNS_S32 output_create(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    const OutputConfig *cfg = MODULE_CONFIG(module, OutputConfig);
    RKNS_S32 ret;
    int ci;

    rkos_memset(&state->outputState, 0, sizeof(state->outputState));
    state->outputState.chnCount = cfg->chnCount;

    /* 动态分配状态数组 */
    if (cfg->chnCount > 0) {
        state->outputState.chnEnabled = (RKNS_BOOL *)rkos_kmalloc(sizeof(RKNS_BOOL) * cfg->chnCount);
        if (!state->outputState.chnEnabled) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for OUTPUT channel states\n");
            output_free_config((OutputConfig*)cfg);
            return RKNS_FAILURE;
        }
        rkos_memset(state->outputState.chnEnabled, 0, sizeof(RKNS_BOOL) * cfg->chnCount);
    }

    for (ci = 0; ci < (int)cfg->chnCount; ci++) {
        const OutputChannelConfig *chn = &cfg->chnConfigs[ci];
        const RKNSOutputParam *param = &chn->param;
        RKNSOutputParam createParam;

        /* 复制一份可修改的 param */
        rkos_memset(&createParam, 0, sizeof(createParam));
        createParam.type = param->type;
        createParam.watchDepth = param->watchDepth;

        switch (createParam.type) {
        case RKNS_OUTPUT_TYPE_APP:
            createParam.app.cmd = param->app.cmd;
            if (param->app.cmd == RKNS_APP_CMD_SAVE_FILE) {
                createParam.app.saveFile.filePath = param->app.saveFile.filePath;
            }
            break;
        case RKNS_OUTPUT_TYPE_ROCKIT:
            createParam.rockit = param->rockit;
            break;
        case RKNS_OUTPUT_TYPE_GSTREAM:
            createParam.gstream = param->gstream;
            if (!createParam.gstream.pipeline) {
                createParam.gstream.pipeline = "autovideosink";
            }
            break;
        case RKNS_OUTPUT_TYPE_OPENCV:
            createParam.opencv = param->opencv;
            if (!createParam.opencv.windowName) {
                createParam.opencv.windowName = "RKNNStream Launch";
            }
            break;
        default:
            fprintf(stderr, "[ERROR] Unsupported output type: %d\n", createParam.type);
            ret = RKNS_FAILURE;
            goto error_cleanup;
        }

        ret = RKNS_OUTPUT_CreateChn(module->devId, chn->chnId, &createParam);
        if (ret != RKNS_SUCCESS) {
            fprintf(stderr, "[ERROR] RKNS_OUTPUT_CreateChn failed for channel %d: %d\n", ci, ret);
            goto error_cleanup;
        }

        state->outputState.chnEnabled[ci] = RKNS_TRUE;
        printf("[INFO] OUTPUT channel %d created (chn_id=%d)\n", ci, chn->chnId);
    }

    /* 设置默认通道信息（保持向后兼容） */
    if (cfg->chnCount > 0) {
        state->chn.enModId = RKNS_ID_OUTPUT;
        state->chn.s32DevId = module->devId;
        state->chn.s32ChnId = cfg->chnConfigs[0].chnId;
    }
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;

error_cleanup:
    for (int j = 0; j < ci; j++) {
        if (state->outputState.chnEnabled[j]) {
            RKNS_OUTPUT_DestroyChn(module->devId, cfg->chnConfigs[j].chnId);
            state->outputState.chnEnabled[j] = RKNS_FALSE;
        }
    }
    if (state->outputState.chnEnabled) {
        rkos_kfree(state->outputState.chnEnabled);
        state->outputState.chnEnabled = NULL;
    }
    state->outputState.chnCount = 0;
    output_free_config((OutputConfig*)cfg);
    return RKNS_FAILURE;
}

RKNS_S32 output_destroy(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    const OutputConfig *cfg = MODULE_CONFIG(module, OutputConfig);

    if (state->created) {
        /* 销毁所有 OUTPUT 通道 */
        for (int ci = (int)cfg->chnCount - 1; ci >= 0; ci--) {
            if (state->outputState.chnEnabled && state->outputState.chnEnabled[ci]) {
                RKNS_OUTPUT_DestroyChn(module->devId, cfg->chnConfigs[ci].chnId);
                state->outputState.chnEnabled[ci] = RKNS_FALSE;
                printf("[INFO] OUTPUT channel %d destroyed (chn_id=%d)\n", ci, cfg->chnConfigs[ci].chnId);
            }
        }

        /* 释放状态数组 */
        if (state->outputState.chnEnabled != NULL) {
            rkos_kfree(state->outputState.chnEnabled);
            state->outputState.chnEnabled = NULL;
        }

        /* 释放配置内存 */
        output_free_config((OutputConfig*)cfg);

        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 output_start(const RKNSLaunchModule *module,
                              RKNSLaunchModuleState *state) {
    const OutputConfig *cfg = MODULE_CONFIG(module, OutputConfig);
    RKNS_S32 ret = RKNS_SUCCESS;
    int ci;

    for (ci = 0; ci < (int)cfg->chnCount; ci++) {
        if (state->outputState.chnEnabled && state->outputState.chnEnabled[ci]) {
            ret = RKNS_OUTPUT_StartChn(module->devId, cfg->chnConfigs[ci].chnId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] RKNS_OUTPUT_StartChn failed for channel %d: %d\n", ci, ret);
                return ret;
            }
        }
    }
    state->started = RKNS_TRUE;
    return ret;
}

RKNS_S32 output_stop(const RKNSLaunchModule *module,
                             RKNSLaunchModuleState *state) {
    const OutputConfig *cfg = MODULE_CONFIG(module, OutputConfig);
    RKNS_S32 ret = RKNS_SUCCESS;
    int ci;

    for (ci = (int)cfg->chnCount - 1; ci >= 0; ci--) {
        if (state->outputState.chnEnabled && state->outputState.chnEnabled[ci]) {
            ret = RKNS_OUTPUT_StopChn(module->devId, cfg->chnConfigs[ci].chnId);
            if (ret != RKNS_SUCCESS) {
                fprintf(stderr, "[ERROR] RKNS_OUTPUT_StopChn failed for channel %d: %d\n", ci, ret);
                /* 继续尝试停止其他通道，不中断 */
            }
        }
    }
    state->started = RKNS_FALSE;
    return ret;
}

/* ---------- Rockit VI 模块 ---------- */

RKNS_S32 rockit_vi_parse_config(YamlDocument *doc, const char *base_path,
                                        RKNSLaunchModule *module) {
    RockitVIConfig *cfg = MODULE_CONFIG(module, RockitVIConfig);
    char path[256];
    int dev_count, pipe_count, chn_count;
    int di, pi, ci;

    rkos_memset(cfg, 0, sizeof(RockitVIConfig));

    /* ========== 获取设备数量并分配内存 ========== */
    snprintf(path, sizeof(path), "%s.devices", base_path);
    dev_count = yaml_get_count(doc, path);
    if (dev_count <= 0) dev_count = 1;

    cfg->devCount = (RKNS_U32)dev_count;
    cfg->devConfigs = (RockitVIDevConfig*)rkos_kmalloc(sizeof(RockitVIDevConfig) * dev_count);
    if (!cfg->devConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for %d VI devices\n", dev_count);
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->devConfigs, 0, sizeof(RockitVIDevConfig) * dev_count);

    /* ========== 解析每个设备 ========== */
    for (di = 0; di < dev_count; di++) {
        RockitVIDevConfig *dev = &cfg->devConfigs[di];

        snprintf(path, sizeof(path), "%s.devices.%d.dev_id", base_path, di);
        dev->devId = (RKNS_S32)yaml_get_int(doc, path, di);

        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.frame_rate", base_path, di);
        dev->frameRate = (RKNS_U32)yaml_get_int(doc, path, 0);

        /* ========== 获取该设备的 pipe 数量并分配内存 ========== */
        snprintf(path, sizeof(path), "%s.devices.%d.pipes", base_path, di);
        pipe_count = yaml_get_count(doc, path);
        if (pipe_count <= 0) pipe_count = 1;

        dev->pipeCount = (RKNS_U32)pipe_count;
        dev->pipeConfigs = (RockitVIPipeConfig*)rkos_kmalloc(sizeof(RockitVIPipeConfig) * pipe_count);
        if (!dev->pipeConfigs) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for %d VI pipes\n", pipe_count);
            for (int j = 0; j < di; j++) {
                if (cfg->devConfigs[j].pipeConfigs) {
                    for (int k = 0; k < (int)cfg->devConfigs[j].pipeCount; k++) {
                        if (cfg->devConfigs[j].pipeConfigs[k].chnConfigs)
                            rkos_kfree(cfg->devConfigs[j].pipeConfigs[k].chnConfigs);
                    }
                    rkos_kfree(cfg->devConfigs[j].pipeConfigs);
                }
            }
            rkos_kfree(cfg->devConfigs);
            cfg->devConfigs = NULL;
            cfg->devCount = 0;
            return RKNS_FAILURE;
        }
        rkos_memset(dev->pipeConfigs, 0, sizeof(RockitVIPipeConfig) * pipe_count);

        /* ========== 解析该设备的所有 pipe ========== */
        for (pi = 0; pi < pipe_count; pi++) {
            RockitVIPipeConfig *pipe = &dev->pipeConfigs[pi];

            snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.pipe_id", base_path, di, pi);
            pipe->pipeId = (RKNS_S32)yaml_get_int(doc, path, pi);

            /* ========== 获取该 pipe 的通道数量并分配内存 ========== */
            snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.channels", base_path, di, pi);
            chn_count = yaml_get_count(doc, path);
            if (chn_count <= 0) chn_count = 1;

            pipe->chnCount = (RKNS_U32)chn_count;
            pipe->chnConfigs = (RockitVIChnConfig*)rkos_kmalloc(sizeof(RockitVIChnConfig) * chn_count);
            if (!pipe->chnConfigs) {
                fprintf(stderr, "[ERROR] Failed to allocate memory for %d VI channels\n", chn_count);
                for (int k = 0; k < pi; k++) {
                    if (dev->pipeConfigs[k].chnConfigs) {
                        rkos_kfree(dev->pipeConfigs[k].chnConfigs);
                        dev->pipeConfigs[k].chnConfigs = NULL;
                    }
                }
                rkos_kfree(dev->pipeConfigs);
                dev->pipeConfigs = NULL;
                for (int j = 0; j < di; j++) {
                    if (cfg->devConfigs[j].pipeConfigs) {
                        for (int k = 0; k < (int)cfg->devConfigs[j].pipeCount; k++) {
                            if (cfg->devConfigs[j].pipeConfigs[k].chnConfigs)
                                rkos_kfree(cfg->devConfigs[j].pipeConfigs[k].chnConfigs);
                        }
                        rkos_kfree(cfg->devConfigs[j].pipeConfigs);
                    }
                }
                rkos_kfree(cfg->devConfigs);
                cfg->devConfigs = NULL;
                cfg->devCount = 0;
                return RKNS_FAILURE;
            }
            rkos_memset(pipe->chnConfigs, 0, sizeof(RockitVIChnConfig) * chn_count);

            /* ========== 解析该 pipe 的所有通道 ========== */
            for (ci = 0; ci < chn_count; ci++) {
                RockitVIChnConfig *chn = &pipe->chnConfigs[ci];

                snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.channels.%d.chn_id", base_path, di, pi, ci);
                chn->chnId = (RKNS_S32)yaml_get_int(doc, path, ci);

                snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.channels.%d.chn_config.width", base_path, di, pi, ci);
                chn->width = (RKNS_U32)yaml_get_int(doc, path, 1920);

                snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.channels.%d.chn_config.height", base_path, di, pi, ci);
                chn->height = (RKNS_U32)yaml_get_int(doc, path, 1080);

                snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.channels.%d.chn_config.buffer_count", base_path, di, pi, ci);
                chn->bufferCount = (RKNS_U32)yaml_get_int(doc, path, 2);

                snprintf(path, sizeof(path), "%s.devices.%d.pipes.%d.channels.%d.chn_config.depth", base_path, di, pi, ci);
                chn->depth = (RKNS_U32)yaml_get_int(doc, path, 0);
            }
        }
    }

    /* 打印 VI 模块配置参数 */
    printf("[INFO] VI Module Config:\n");
    printf("[INFO]   Total devices: %u\n", cfg->devCount);
    for (di = 0; di < (int)cfg->devCount; di++) {
        RockitVIDevConfig *dev = &cfg->devConfigs[di];
        printf("[INFO]   Device[%d]: dev_id=%d, frame_rate=%u\n", di, dev->devId, dev->frameRate);
        for (pi = 0; pi < (int)dev->pipeCount; pi++) {
            RockitVIPipeConfig *pipe = &dev->pipeConfigs[pi];
            printf("[INFO]     Pipe[%d]: pipe_id=%d\n", pi, pipe->pipeId);
            for (ci = 0; ci < (int)pipe->chnCount; ci++) {
                RockitVIChnConfig *chn = &pipe->chnConfigs[ci];
                printf("[INFO]       Channel[%d]: chn_id=%d, %ux%u, buffer_count=%u, depth=%u\n",
                       ci, chn->chnId, chn->width, chn->height, chn->bufferCount, chn->depth);
            }
        }
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vi_create(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    const RockitVIConfig *cfg = MODULE_CONFIG(module, RockitVIConfig);
    RK_S32 ret;
    int di = 0, pi = 0, ci = 0;

    if (cfg->devCount == 0 || cfg->devConfigs == NULL) {
        fprintf(stderr, "[ERROR] No VI device config available\n");
        return RKNS_FAILURE;
    }

    rkos_memset(&state->viState, 0, sizeof(state->viState));
    state->viState.devCount = cfg->devCount;

    printf("[INFO] Creating VI module with %u devices\n", cfg->devCount);

    /* ========== 遍历所有设备 ========== */
    for (di = 0; di < (int)cfg->devCount && di < MAX_VI_DEVICES; di++) {
        RockitVIDevConfig *dev = &cfg->devConfigs[di];

        if (dev->pipeCount == 0 || dev->pipeConfigs == NULL) {
            fprintf(stderr, "[WARNING] VI device %d has no pipes, skipping\n", di);
            continue;
        }

        state->viState.pipeCount[di] = dev->pipeCount;
        for (pi = 0; pi < (int)dev->pipeCount && pi < MAX_VI_PIPES; pi++)
            state->viState.chnCount[di][pi] = dev->pipeConfigs[pi].chnCount;

        /* 1. 配置 VI 设备 */
        {
            VI_DEV_ATTR_S stDevAttr;
            rkos_memset(&stDevAttr, 0, sizeof(stDevAttr));
            ret = RK_MPI_VI_GetDevAttr(dev->devId, &stDevAttr);
            if (ret == RK_ERR_VI_NOT_CONFIG) {
                ret = RK_MPI_VI_SetDevAttr(dev->devId, &stDevAttr);
                if (ret != 0) {
                    fprintf(stderr, "[ERROR] RK_MPI_VI_SetDevAttr failed: %#x\n", ret);
                    goto cleanup_devices;
                }
            }
        }

        /* 2. 使能 VI 设备 */
        ret = RK_MPI_VI_GetDevIsEnable(dev->devId);
        if (ret != 0) {
            ret = RK_MPI_VI_EnableDev(dev->devId);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RK_MPI_VI_EnableDev failed: %#x\n", ret);
                goto cleanup_devices;
            }
            state->viState.devEnabled[di] = RKNS_TRUE;
        } else {
            state->viState.devEnabled[di] = RKNS_TRUE;
        }

        /* 3. 绑定设备到所有 pipe */
        {
            VI_DEV_BIND_PIPE_S stBindPipe;
            int bindCount = RK_MIN((int)dev->pipeCount, MAX_VI_BIND_PIPE_NUM);
            rkos_memset(&stBindPipe, 0, sizeof(stBindPipe));
            stBindPipe.u32Num = 1;
            for (pi = 0; pi < bindCount; pi++)
                stBindPipe.PipeId[pi] = dev->pipeConfigs[pi].pipeId;
            ret = RK_MPI_VI_SetDevBindPipe(dev->devId, &stBindPipe);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RK_MPI_VI_SetDevBindPipe failed: %#x\n", ret);
                goto cleanup_devices;
            }
        }

        /* ========== 遍历该设备的所有 pipe 和通道 ========== */
        for (pi = 0; pi < (int)dev->pipeCount && pi < MAX_VI_PIPES; pi++) {
            RockitVIPipeConfig *pipe = &dev->pipeConfigs[pi];
            if (pipe->chnCount == 0 || pipe->chnConfigs == NULL)
                continue;

            for (ci = 0; ci < (int)pipe->chnCount && ci < MAX_VI_CHNS; ci++) {
                RockitVIChnConfig *chn = &pipe->chnConfigs[ci];
                VI_CHN_ATTR_S stChnAttr;

                rkos_memset(&stChnAttr, 0, sizeof(stChnAttr));
                stChnAttr.stSize.u32Width = chn->width;
                stChnAttr.stSize.u32Height = chn->height;
                stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
                stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
                stChnAttr.u32Depth = chn->depth;
                stChnAttr.stIspOpt.u32BufCount = chn->bufferCount;
                stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;

                ret = RK_MPI_VI_SetChnAttr(pipe->pipeId, chn->chnId, &stChnAttr);
                if (ret != 0) {
                    fprintf(stderr, "[ERROR] RK_MPI_VI_SetChnAttr failed: %#x\n", ret);
                    goto cleanup_channels;
                }

                ret = RK_MPI_VI_EnableChn(pipe->pipeId, chn->chnId);
                if (ret != 0) {
                    fprintf(stderr, "[ERROR] RK_MPI_VI_EnableChn failed: %#x\n", ret);
                    goto cleanup_channels;
                }

                state->viState.chnEnabled[di][pi][ci] = RKNS_TRUE;
                printf("[INFO]   VI channel enabled: dev=%d, pipe=%d, chn=%d, %ux%u\n",
                       dev->devId, pipe->pipeId, chn->chnId, chn->width, chn->height);
            }
        }
    }

    /* 使用第一个设备和第一个通道作为模块的主通道（用于绑定） */
    if (cfg->devCount > 0 && cfg->devConfigs[0].pipeCount > 0 &&
        cfg->devConfigs[0].pipeConfigs[0].chnCount > 0) {
        state->chn.s32DevId = cfg->devConfigs[0].devId;
        state->chn.s32ChnId = cfg->devConfigs[0].pipeConfigs[0].chnConfigs[0].chnId;
    } else {
        fprintf(stderr, "[ERROR] VI module has no valid device/pipe/channel\n");
        goto cleanup_devices;
    }
    state->created = RKNS_TRUE;
    printf("[INFO] VI module created successfully\n");
    return RKNS_SUCCESS;

cleanup_channels:
    /* 清理当前设备 di 的已创建通道，然后禁用设备 */
    {
        RockitVIDevConfig *cleanupDev = &cfg->devConfigs[di];
        for (int p = pi; p >= 0; p--) {
            int cEnd = (p == pi) ? ci - 1 : RK_MIN((int)cleanupDev->pipeConfigs[p].chnCount, MAX_VI_CHNS) - 1;
            for (int c = cEnd; c >= 0; c--) {
                if (state->viState.chnEnabled[di][p][c]) {
                    RK_MPI_VI_DisableChn(cleanupDev->pipeConfigs[p].pipeId,
                                          cleanupDev->pipeConfigs[p].chnConfigs[c].chnId);
                    state->viState.chnEnabled[di][p][c] = RKNS_FALSE;
                }
            }
        }
        if (state->viState.devEnabled[di]) {
            RK_MPI_VI_DisableDev(cleanupDev->devId);
            state->viState.devEnabled[di] = RKNS_FALSE;
        }
    }

cleanup_devices:
    /* 清理之前已创建的设备（d < di） */
    for (int d = di; d >= 0; d--) {
        RockitVIDevConfig *cleanupDev = &cfg->devConfigs[d];
        if (state->viState.devEnabled[d]) {
            for (int p = 0; p < (int)state->viState.pipeCount[d] && p < MAX_VI_PIPES; p++) {
                for (int c = 0; c < (int)state->viState.chnCount[d][p] && c < MAX_VI_CHNS; c++) {
                    if (state->viState.chnEnabled[d][p][c]) {
                        RK_MPI_VI_DisableChn(cleanupDev->pipeConfigs[p].pipeId,
                                              cleanupDev->pipeConfigs[p].chnConfigs[c].chnId);
                        state->viState.chnEnabled[d][p][c] = RKNS_FALSE;
                    }
                }
            }
            RK_MPI_VI_DisableDev(cleanupDev->devId);
            state->viState.devEnabled[d] = RKNS_FALSE;
        }
    }
    return RKNS_FAILURE;
}

RKNS_S32 rockit_vi_destroy(const RKNSLaunchModule *module,
                                   RKNSLaunchModuleState *state) {
    RockitVIConfig *cfg = MODULE_CONFIG(module, RockitVIConfig);
    int di, pi, ci;

    printf("[INFO] Destroying VI module\n");

    /* 反向清理所有通道和设备 */
    if (cfg->devCount > 0 && cfg->devConfigs != NULL) {
        for (di = RK_MIN((int)cfg->devCount, MAX_VI_DEVICES) - 1; di >= 0; di--) {
            RockitVIDevConfig *dev = &cfg->devConfigs[di];
            if (!state->viState.devEnabled[di])
                continue;

            /* 反向禁用所有通道 */
            for (pi = RK_MIN((int)dev->pipeCount, MAX_VI_PIPES) - 1; pi >= 0; pi--) {
                RockitVIPipeConfig *pipe = &dev->pipeConfigs[pi];
                for (ci = RK_MIN((int)pipe->chnCount, MAX_VI_CHNS) - 1; ci >= 0; ci--) {
                    if (state->viState.chnEnabled[di][pi][ci]) {
                        RK_MPI_VI_DisableChn(pipe->pipeId, pipe->chnConfigs[ci].chnId);
                        state->viState.chnEnabled[di][pi][ci] = RKNS_FALSE;
                    }
                }
            }

            RK_MPI_VI_DisableDev(dev->devId);
            state->viState.devEnabled[di] = RKNS_FALSE;
        }

        /* 释放动态分配的内存 */
        for (di = 0; di < (int)cfg->devCount; di++) {
            RockitVIDevConfig *dev = &cfg->devConfigs[di];
            if (dev->pipeConfigs != NULL) {
                for (pi = 0; pi < (int)dev->pipeCount; pi++) {
                    if (dev->pipeConfigs[pi].chnConfigs != NULL) {
                        rkos_kfree(dev->pipeConfigs[pi].chnConfigs);
                        dev->pipeConfigs[pi].chnConfigs = NULL;
                    }
                }
                rkos_kfree(dev->pipeConfigs);
                dev->pipeConfigs = NULL;
            }
        }
        rkos_kfree(cfg->devConfigs);
        cfg->devConfigs = NULL;
    }
    cfg->devCount = 0;

    state->created = RKNS_FALSE;
    printf("[INFO] VI module destroyed\n");
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
    const char *str;
    int dev_count, layer_count, chn_count;
    int di, li, ci;

    rkos_memset(cfg, 0, sizeof(RockitVOConfig));

    /* ========== 获取设备数量并分配内存 ========== */
    snprintf(path, sizeof(path), "%s.devices", base_path);
    dev_count = yaml_get_count(doc, path);
    if (dev_count <= 0) dev_count = 1;

    cfg->devCount = (RKNS_U32)dev_count;
    cfg->devConfigs = (RockitVODevConfig*)rkos_kmalloc(sizeof(RockitVODevConfig) * dev_count);
    if (!cfg->devConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for %d VO devices\n", dev_count);
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->devConfigs, 0, sizeof(RockitVODevConfig) * dev_count);

    /* ========== 解析每个设备 ========== */
    for (di = 0; di < dev_count; di++) {
        RockitVODevConfig *dev = &cfg->devConfigs[di];

        snprintf(path, sizeof(path), "%s.devices.%d.dev_id", base_path, di);
        dev->devId = (RKNS_S32)yaml_get_int(doc, path, di);

        /* 接口类型 */
        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.intf_type", base_path, di);
        str = yaml_get_string(doc, path, "MIPI");
        if (strcmp(str, "HDMI") == 0)
            dev->intfType = VO_INTF_HDMI;
        else if (strcmp(str, "MIPI") == 0)
            dev->intfType = VO_INTF_MIPI;
        else if (strcmp(str, "LCD") == 0)
            dev->intfType = VO_INTF_LCD;
        else
            dev->intfType = VO_INTF_MIPI;

        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.intf_sync", base_path, di);
        dev->intfSync = (RKNS_S32)yaml_get_int(doc, path, 0);

        snprintf(path, sizeof(path), "%s.devices.%d.dev_config.bg_color", base_path, di);
        dev->bgColor = (RKNS_U32)yaml_get_int(doc, path, 0);

        /* ========== 获取该设备的图层数量并分配内存 ========== */
        snprintf(path, sizeof(path), "%s.devices.%d.layers", base_path, di);
        layer_count = yaml_get_count(doc, path);
        if (layer_count <= 0) layer_count = 1;

        dev->layerCount = (RKNS_U32)layer_count;
        dev->layerConfigs = (RockitVOLayerConfig*)rkos_kmalloc(sizeof(RockitVOLayerConfig) * layer_count);
        if (!dev->layerConfigs) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for %d VO layers\n", layer_count);
            for (int j = 0; j < di; j++) {
                if (cfg->devConfigs[j].layerConfigs) {
                    for (int k = 0; k < (int)cfg->devConfigs[j].layerCount; k++) {
                        if (cfg->devConfigs[j].layerConfigs[k].chnConfigs)
                            rkos_kfree(cfg->devConfigs[j].layerConfigs[k].chnConfigs);
                    }
                    rkos_kfree(cfg->devConfigs[j].layerConfigs);
                }
            }
            rkos_kfree(cfg->devConfigs);
            cfg->devConfigs = NULL;
            cfg->devCount = 0;
            return RKNS_FAILURE;
        }
        rkos_memset(dev->layerConfigs, 0, sizeof(RockitVOLayerConfig) * layer_count);

        /* ========== 解析该设备的所有图层 ========== */
        for (li = 0; li < layer_count; li++) {
            RockitVOLayerConfig *layer = &dev->layerConfigs[li];

            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.layer_id", base_path, di, li);
            layer->layerId = (RKNS_S32)yaml_get_int(doc, path, li);

            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.layer_config.layer_width", base_path, di, li);
            layer->layerWidth = (RKNS_U32)yaml_get_int(doc, path, 1920);

            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.layer_config.layer_height", base_path, di, li);
            layer->layerHeight = (RKNS_U32)yaml_get_int(doc, path, 1080);

            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.layer_config.disp_width", base_path, di, li);
            layer->dispWidth = (RKNS_U32)yaml_get_int(doc, path, layer->layerWidth);

            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.layer_config.disp_height", base_path, di, li);
            layer->dispHeight = (RKNS_U32)yaml_get_int(doc, path, layer->layerHeight);

            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.layer_config.layer_mode", base_path, di, li);
            layer->layerMode = (RKNS_S32)yaml_get_int(doc, path, 0);

            /* ========== 获取该图层的通道数量并分配内存 ========== */
            snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels", base_path, di, li);
            chn_count = yaml_get_count(doc, path);
            if (chn_count <= 0) chn_count = 1;

            layer->chnCount = (RKNS_U32)chn_count;
            layer->chnConfigs = (RockitVOChnConfig*)rkos_kmalloc(sizeof(RockitVOChnConfig) * chn_count);
            if (!layer->chnConfigs) {
                fprintf(stderr, "[ERROR] Failed to allocate memory for %d VO channels\n", chn_count);
                for (int k = 0; k < li; k++) {
                    if (dev->layerConfigs[k].chnConfigs) {
                        rkos_kfree(dev->layerConfigs[k].chnConfigs);
                        dev->layerConfigs[k].chnConfigs = NULL;
                    }
                }
                rkos_kfree(dev->layerConfigs);
                dev->layerConfigs = NULL;
                for (int j = 0; j < di; j++) {
                    if (cfg->devConfigs[j].layerConfigs) {
                        for (int k = 0; k < (int)cfg->devConfigs[j].layerCount; k++) {
                            if (cfg->devConfigs[j].layerConfigs[k].chnConfigs)
                                rkos_kfree(cfg->devConfigs[j].layerConfigs[k].chnConfigs);
                        }
                        rkos_kfree(cfg->devConfigs[j].layerConfigs);
                    }
                }
                rkos_kfree(cfg->devConfigs);
                cfg->devConfigs = NULL;
                cfg->devCount = 0;
                return RKNS_FAILURE;
            }
            rkos_memset(layer->chnConfigs, 0, sizeof(RockitVOChnConfig) * chn_count);

            /* ========== 解析该图层的所有通道 ========== */
            for (ci = 0; ci < chn_count; ci++) {
                RockitVOChnConfig *chn = &layer->chnConfigs[ci];

                snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels.%d.chn_id", base_path, di, li, ci);
                chn->chnId = (RKNS_S32)yaml_get_int(doc, path, ci);

                snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels.%d.chn_config.width", base_path, di, li, ci);
                chn->width = (RKNS_U32)yaml_get_int(doc, path, layer->layerWidth);

                snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels.%d.chn_config.height", base_path, di, li, ci);
                chn->height = (RKNS_U32)yaml_get_int(doc, path, layer->layerHeight);

                snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels.%d.chn_config.priority", base_path, di, li, ci);
                chn->priority = (RKNS_U32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels.%d.chn_config.fg_alpha", base_path, di, li, ci);
                chn->fgAlpha = (RKNS_U32)yaml_get_int(doc, path, 255);

                snprintf(path, sizeof(path), "%s.devices.%d.layers.%d.channels.%d.chn_config.bg_alpha", base_path, di, li, ci);
                chn->bgAlpha = (RKNS_U32)yaml_get_int(doc, path, 0);
            }
        }
    }

    /* 打印 VO 模块配置参数 */
    printf("[INFO] VO Module Config:\n");
    printf("[INFO]   Total devices: %u\n", cfg->devCount);
    for (di = 0; di < (int)cfg->devCount; di++) {
        RockitVODevConfig *dev = &cfg->devConfigs[di];
        printf("[INFO]   Device[%d]: dev_id=%d, intf_type=%d, intf_sync=%d\n",
               di, dev->devId, dev->intfType, dev->intfSync);
        for (li = 0; li < (int)dev->layerCount; li++) {
            RockitVOLayerConfig *layer = &dev->layerConfigs[li];
            printf("[INFO]     Layer[%d]: layer_id=%d, %ux%u, disp=%ux%u, mode=%d\n",
                   li, layer->layerId, layer->layerWidth, layer->layerHeight,
                   layer->dispWidth, layer->dispHeight, layer->layerMode);
            for (ci = 0; ci < (int)layer->chnCount; ci++) {
                RockitVOChnConfig *chn = &layer->chnConfigs[ci];
                printf("[INFO]       Channel[%d]: chn_id=%d, %ux%u, priority=%u, alpha=%u/%u\n",
                       ci, chn->chnId, chn->width, chn->height, chn->priority, chn->fgAlpha, chn->bgAlpha);
            }
        }
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vo_create(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    const RockitVOConfig *cfg = MODULE_CONFIG(module, RockitVOConfig);
    RK_S32 ret;
    int di = 0, li = 0, ci = 0;

    if (cfg->devCount == 0 || cfg->devConfigs == NULL) {
        fprintf(stderr, "[ERROR] No VO device config available\n");
        return RKNS_FAILURE;
    }

    rkos_memset(&state->voState, 0, sizeof(state->voState));
    state->voState.devCount = cfg->devCount;

    printf("[INFO] Creating VO module with %u devices\n", cfg->devCount);

    /* ========== 遍历所有设备 ========== */
    for (di = 0; di < (int)cfg->devCount && di < MAX_VO_DEVICES; di++) {
        RockitVODevConfig *dev = &cfg->devConfigs[di];

        if (dev->layerCount == 0 || dev->layerConfigs == NULL) {
            fprintf(stderr, "[WARNING] VO device %d has no layers, skipping\n", di);
            continue;
        }

        state->voState.layerCount[di] = dev->layerCount;

        /* 1. 绑定所有图层到设备 */
        for (li = 0; li < (int)dev->layerCount && li < MAX_VO_LAYERS; li++) {
            RockitVOLayerConfig *layer = &dev->layerConfigs[li];
            ret = RK_MPI_VO_BindLayer(layer->layerId, dev->devId, layer->layerMode);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RK_MPI_VO_BindLayer failed: dev=%d layer=%d, %#x\n",
                        dev->devId, layer->layerId, ret);
                goto cleanup;
            }
            state->voState.layerBound[di][li] = RKNS_TRUE;
        }

        /* 2. 设置 VO 设备公共属性 */
        {
            VO_PUB_ATTR_S stPubAttr;
            rkos_memset(&stPubAttr, 0, sizeof(stPubAttr));
            stPubAttr.enIntfType = dev->intfType;
            stPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
            ret = RK_MPI_VO_SetPubAttr(dev->devId, &stPubAttr);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RK_MPI_VO_SetPubAttr failed: dev=%d, %#x\n",
                        dev->devId, ret);
                goto cleanup;
            }
        }

        /* 3. 使能 VO 设备 */
        ret = RK_MPI_VO_Enable(dev->devId);
        if (ret != 0) {
            fprintf(stderr, "[ERROR] RK_MPI_VO_Enable failed: dev=%d, %#x\n", dev->devId, ret);
            goto cleanup;
        }
        state->voState.devEnabled[di] = RKNS_TRUE;

        /* ========== 遍历该设备的所有图层和通道 ========== */
        for (li = 0; li < (int)dev->layerCount && li < MAX_VO_LAYERS; li++) {
            RockitVOLayerConfig *layer = &dev->layerConfigs[li];

            state->voState.chnCount[di][li] = layer->chnCount;

            /* 4. 设置图层属性 */
            {
                VO_VIDEO_LAYER_ATTR_S stLayerAttr;
                rkos_memset(&stLayerAttr, 0, sizeof(stLayerAttr));
                stLayerAttr.enPixFormat = RK_FMT_BGRA8888;
                stLayerAttr.enCompressMode = COMPRESS_AFBC_16x16;
                stLayerAttr.u32DispFrmRt = 25;
                stLayerAttr.stDispRect.s32X = 0;
                stLayerAttr.stDispRect.s32Y = 0;
                stLayerAttr.stDispRect.u32Width = layer->dispWidth;
                stLayerAttr.stDispRect.u32Height = layer->dispHeight;
                stLayerAttr.stImageSize.u32Width = layer->layerWidth;
                stLayerAttr.stImageSize.u32Height = layer->layerHeight;
                RK_MPI_VO_SetLayerDispBufLen(layer->layerId, 3);
                ret = RK_MPI_VO_SetLayerAttr(layer->layerId, &stLayerAttr);
                if (ret != 0) {
                    fprintf(stderr, "[ERROR] RK_MPI_VO_SetLayerAttr failed: layer=%d, %#x\n",
                            layer->layerId, ret);
                    goto cleanup;
                }
            }

            /* 5. 使能图层 */
            RK_MPI_VO_SetLayerSpliceMode(layer->layerId, VO_SPLICE_MODE_RGA);
            ret = RK_MPI_VO_EnableLayer(layer->layerId);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RK_MPI_VO_EnableLayer failed: layer=%d, %#x\n",
                        layer->layerId, ret);
                goto cleanup;
            }
            state->voState.layerEnabled[di][li] = RKNS_TRUE;

            /* 6. 设置通道属性并使能 */
            if (layer->chnCount > 0 && layer->chnConfigs != NULL) {
                for (ci = 0; ci < (int)layer->chnCount && ci < MAX_VO_CHNS; ci++) {
                    RockitVOChnConfig *chn = &layer->chnConfigs[ci];
                    VO_CHN_ATTR_S stChnAttr;

                    rkos_memset(&stChnAttr, 0, sizeof(stChnAttr));
                    stChnAttr.stRect.s32X = 0;
                    stChnAttr.stRect.s32Y = 0;
                    stChnAttr.stRect.u32Width = chn->width;
                    stChnAttr.stRect.u32Height = chn->height;
                    stChnAttr.u32Priority = chn->priority;
                    stChnAttr.u32FgAlpha = chn->fgAlpha;
                    stChnAttr.u32BgAlpha = chn->bgAlpha;

                    ret = RK_MPI_VO_SetChnAttr(layer->layerId, chn->chnId, &stChnAttr);
                    if (ret != 0) {
                        fprintf(stderr, "[ERROR] RK_MPI_VO_SetChnAttr failed: layer=%d chn=%d, %#x\n",
                                layer->layerId, chn->chnId, ret);
                        goto cleanup;
                    }

                    ret = RK_MPI_VO_EnableChn(layer->layerId, chn->chnId);
                    if (ret != 0) {
                        fprintf(stderr, "[ERROR] RK_MPI_VO_EnableChn failed: layer=%d chn=%d, %#x\n",
                                layer->layerId, chn->chnId, ret);
                        goto cleanup;
                    }

                    state->voState.chnEnabled[di][li][ci] = RKNS_TRUE;
                    printf("[INFO]   VO channel enabled: dev=%d, layer=%d, chn=%d, %ux%u\n",
                           dev->devId, layer->layerId, chn->chnId, chn->width, chn->height);
                }
            }

            printf("[INFO]   VO layer enabled: dev=%d, layer=%d, %ux%u -> disp %ux%u\n",
                   dev->devId, layer->layerId, layer->layerWidth, layer->layerHeight,
                   layer->dispWidth, layer->dispHeight);
        }
    }

    /* 使用第一个设备和第一个图层/通道作为模块的主通道（用于绑定） */
    if (cfg->devCount > 0 && cfg->devConfigs[0].layerCount > 0 &&
        cfg->devConfigs[0].layerConfigs[0].chnCount > 0) {
        state->chn.s32DevId = cfg->devConfigs[0].layerConfigs[0].layerId;
        state->chn.s32ChnId = cfg->devConfigs[0].layerConfigs[0].chnConfigs[0].chnId;
    } else {
        fprintf(stderr, "[ERROR] VO module has no valid device/layer/channel\n");
        goto cleanup;
    }
    state->created = RKNS_TRUE;
    printf("[INFO] VO module created successfully\n");
    return RKNS_SUCCESS;

cleanup:
    /* 反向清理已创建的资源 */
    for (int d = di; d >= 0; d--) {
        RockitVODevConfig *cleanupDev = &cfg->devConfigs[d];
        /* 1. 禁用通道和图层 */
        int lEnd = (d == di) ? li : RK_MIN((int)cleanupDev->layerCount, MAX_VO_LAYERS);
        for (int l = lEnd - 1; l >= 0; l--) {
            RockitVOLayerConfig *cleanupLayer = &cleanupDev->layerConfigs[l];
            int cEnd = (d == di && l == li) ? ci : RK_MIN((int)cleanupLayer->chnCount, MAX_VO_CHNS);
            for (int c = cEnd - 1; c >= 0; c--) {
                if (state->voState.chnEnabled[d][l][c]) {
                    RK_MPI_VO_DisableChn(cleanupLayer->layerId, cleanupLayer->chnConfigs[c].chnId);
                    state->voState.chnEnabled[d][l][c] = RKNS_FALSE;
                }
            }
            if (state->voState.layerEnabled[d][l]) {
                RK_MPI_VO_DisableLayer(cleanupLayer->layerId);
                state->voState.layerEnabled[d][l] = RKNS_FALSE;
            }
        }
        /* 2. 禁用设备 */
        if (state->voState.devEnabled[d]) {
            RK_MPI_VO_Disable(cleanupDev->devId);
            state->voState.devEnabled[d] = RKNS_FALSE;
        }
        /* 3. 解绑图层 */
        for (int l = RK_MIN((int)cleanupDev->layerCount, MAX_VO_LAYERS) - 1; l >= 0; l--) {
            if (state->voState.layerBound[d][l]) {
                RK_MPI_VO_UnBindLayer(cleanupDev->layerConfigs[l].layerId, cleanupDev->devId);
                state->voState.layerBound[d][l] = RKNS_FALSE;
            }
        }
    }
    return RKNS_FAILURE;
}

RKNS_S32 rockit_vo_destroy(const RKNSLaunchModule *module,
                                   RKNSLaunchModuleState *state) {
    RockitVOConfig *cfg = MODULE_CONFIG(module, RockitVOConfig);
    int di, li, ci;

    printf("[INFO] Destroying VO module\n");

    if (cfg->devCount > 0 && cfg->devConfigs != NULL) {
        for (di = RK_MIN((int)cfg->devCount, MAX_VO_DEVICES) - 1; di >= 0; di--) {
            RockitVODevConfig *dev = &cfg->devConfigs[di];

            /* 1. 禁用通道和图层 */
            for (li = RK_MIN((int)dev->layerCount, MAX_VO_LAYERS) - 1; li >= 0; li--) {
                RockitVOLayerConfig *layer = &dev->layerConfigs[li];
                for (ci = RK_MIN((int)layer->chnCount, MAX_VO_CHNS) - 1; ci >= 0; ci--) {
                    if (state->voState.chnEnabled[di][li][ci]) {
                        RK_MPI_VO_DisableChn(layer->layerId, layer->chnConfigs[ci].chnId);
                        state->voState.chnEnabled[di][li][ci] = RKNS_FALSE;
                    }
                }
                if (state->voState.layerEnabled[di][li]) {
                    RK_MPI_VO_DisableLayer(layer->layerId);
                    state->voState.layerEnabled[di][li] = RKNS_FALSE;
                }
            }

            /* 2. 禁用设备 */
            if (state->voState.devEnabled[di]) {
                RK_MPI_VO_Disable(dev->devId);
                state->voState.devEnabled[di] = RKNS_FALSE;
            }

            /* 3. 解绑图层 */
            for (li = RK_MIN((int)dev->layerCount, MAX_VO_LAYERS) - 1; li >= 0; li--) {
                if (state->voState.layerBound[di][li]) {
                    RK_MPI_VO_UnBindLayer(dev->layerConfigs[li].layerId, dev->devId);
                    state->voState.layerBound[di][li] = RKNS_FALSE;
                }
            }
        }

        /* 释放所有动态分配的内存（遍历全部，不限于 MAX_VO_DEVICES） */
        for (di = 0; di < (int)cfg->devCount; di++) {
            RockitVODevConfig *dev = &cfg->devConfigs[di];
            if (dev->layerConfigs != NULL) {
                for (li = 0; li < (int)dev->layerCount; li++) {
                    if (dev->layerConfigs[li].chnConfigs != NULL) {
                        rkos_kfree(dev->layerConfigs[li].chnConfigs);
                        dev->layerConfigs[li].chnConfigs = NULL;
                    }
                }
                rkos_kfree(dev->layerConfigs);
                dev->layerConfigs = NULL;
            }
        }
        rkos_kfree(cfg->devConfigs);
        cfg->devConfigs = NULL;
    }
    cfg->devCount = 0;

    state->created = RKNS_FALSE;
    printf("[INFO] VO module destroyed\n");
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

/* ---------- Rockit VPSS 模块 ---------- */

static PIXEL_FORMAT_E parse_vpss_pixel_format(const char *fmt_str) {
    if (strcmp(fmt_str, "NV12") == 0) {
        return RK_FMT_YUV420SP;
    } else if (strcmp(fmt_str, "RGB888") == 0) {
        return RK_FMT_RGB888;
    }
    return RK_FMT_YUV420SP; /* 默认格式 */
}

static const char *vpss_pixel_format_to_string(PIXEL_FORMAT_E fmt) {
    if (fmt == RK_FMT_YUV420SP) {
        return "NV12";
    } else if (fmt == RK_FMT_RGB888) {
        return "RGB888";
    }
    return "NV12"; /* 默认格式名称 */
}

RKNS_S32 rockit_vpss_parse_config(YamlDocument *doc, const char *base_path,
                                        RKNSLaunchModule *module) {
    RockitVPSSConfig *cfg = MODULE_CONFIG(module, RockitVPSSConfig);
    char path[256];
    int grp_count, chn_count;
    int gi, ci;

    rkos_memset(cfg, 0, sizeof(RockitVPSSConfig));

    /* ========== 获取 Group 数量并分配内存 ========== */
    snprintf(path, sizeof(path), "%s.groups", base_path);
    grp_count = yaml_get_count(doc, path);
    if (grp_count <= 0) grp_count = 1;

    cfg->grpCount = (RKNS_U32)grp_count;
    cfg->grpConfigs = (RockitVPSSGrpConfig*)rkos_kmalloc(sizeof(RockitVPSSGrpConfig) * grp_count);
    if (!cfg->grpConfigs) {
        fprintf(stderr, "[ERROR] Failed to allocate memory for %d VPSS groups\n", grp_count);
        return RKNS_FAILURE;
    }
    rkos_memset(cfg->grpConfigs, 0, sizeof(RockitVPSSGrpConfig) * grp_count);

    /* ========== 解析每个 Group ========== */
    for (gi = 0; gi < grp_count; gi++) {
        RockitVPSSGrpConfig *grp = &cfg->grpConfigs[gi];

        snprintf(path, sizeof(path), "%s.groups.%d.grp_id", base_path, gi);
        grp->grpId = (RKNS_S32)yaml_get_int(doc, path, gi);

        snprintf(path, sizeof(path), "%s.groups.%d.grp_config.max_width", base_path, gi);
        grp->maxWidth = (RKNS_U32)yaml_get_int(doc, path, 1920);

        snprintf(path, sizeof(path), "%s.groups.%d.grp_config.max_height", base_path, gi);
        grp->maxHeight = (RKNS_U32)yaml_get_int(doc, path, 1080);

        snprintf(path, sizeof(path), "%s.groups.%d.grp_config.frame_rate", base_path, gi);
        grp->frameRate = (RKNS_U32)yaml_get_int(doc, path, 0);

        /* ========== 获取该 Group 的通道数量并分配内存 ========== */
        snprintf(path, sizeof(path), "%s.groups.%d.channels", base_path, gi);
        chn_count = yaml_get_count(doc, path);
        if (chn_count <= 0) chn_count = 1;

        grp->chnCount = (RKNS_U32)chn_count;
        grp->chnConfigs = (RockitVPSSChnConfig*)rkos_kmalloc(sizeof(RockitVPSSChnConfig) * chn_count);
        if (!grp->chnConfigs) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for %d VPSS channels\n", chn_count);
            for (int j = 0; j < gi; j++) {
                if (cfg->grpConfigs[j].chnConfigs)
                    rkos_kfree(cfg->grpConfigs[j].chnConfigs);
            }
            rkos_kfree(cfg->grpConfigs);
            cfg->grpConfigs = NULL;
            cfg->grpCount = 0;
            return RKNS_FAILURE;
        }
        rkos_memset(grp->chnConfigs, 0, sizeof(RockitVPSSChnConfig) * chn_count);

        /* ========== 解析该 Group 的所有通道 ========== */
        for (ci = 0; ci < chn_count; ci++) {
            RockitVPSSChnConfig *chn = &grp->chnConfigs[ci];

            snprintf(path, sizeof(path), "%s.groups.%d.channels.%d.chn_id", base_path, gi, ci);
            chn->chnId = (RKNS_S32)yaml_get_int(doc, path, ci);

            snprintf(path, sizeof(path), "%s.groups.%d.channels.%d.chn_config.width", base_path, gi, ci);
            chn->width = (RKNS_U32)yaml_get_int(doc, path, 640);

            snprintf(path, sizeof(path), "%s.groups.%d.channels.%d.chn_config.height", base_path, gi, ci);
            chn->height = (RKNS_U32)yaml_get_int(doc, path, 480);

            snprintf(path, sizeof(path), "%s.groups.%d.channels.%d.chn_config.buffer_count", base_path, gi, ci);
            chn->bufferCount = (RKNS_U32)yaml_get_int(doc, path, 4);

            snprintf(path, sizeof(path), "%s.groups.%d.channels.%d.chn_config.depth", base_path, gi, ci);
            chn->depth = (RKNS_U32)yaml_get_int(doc, path, 4);

            snprintf(path, sizeof(path), "%s.groups.%d.channels.%d.chn_config.pixel_format", base_path, gi, ci);
            const char *fmt_str = yaml_get_string(doc, path, "NV12");
            chn->pixelFormat = (RKNS_U32)parse_vpss_pixel_format(fmt_str);
        }
    }

    /* 打印 VPSS 模块配置参数 */
    printf("[INFO] VPSS Module Config:\n");
    printf("[INFO]   Total groups: %u\n", cfg->grpCount);
    for (gi = 0; gi < (int)cfg->grpCount; gi++) {
        RockitVPSSGrpConfig *grp = &cfg->grpConfigs[gi];
        printf("[INFO]   Group[%d]: grp_id=%d, max=%ux%u, frame_rate=%u\n",
               gi, grp->grpId, grp->maxWidth, grp->maxHeight, grp->frameRate);
        for (ci = 0; ci < (int)grp->chnCount; ci++) {
            RockitVPSSChnConfig *chn = &grp->chnConfigs[ci];
            printf("[INFO]     Channel[%d]: chn_id=%d, %ux%u, buffer_count=%u, depth=%u, pixel_format=%s\n",
                   ci, chn->chnId, chn->width, chn->height, chn->bufferCount, chn->depth,
                   vpss_pixel_format_to_string((PIXEL_FORMAT_E)chn->pixelFormat));
        }
    }

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vpss_create(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    const RockitVPSSConfig *cfg = MODULE_CONFIG(module, RockitVPSSConfig);
    RK_S32 ret;
    int gi = 0, ci = 0;

    if (cfg->grpCount == 0 || cfg->grpConfigs == NULL) {
        fprintf(stderr, "[ERROR] No VPSS group config available\n");
        return RKNS_FAILURE;
    }

    rkos_memset(&state->vpssState, 0, sizeof(state->vpssState));
    state->vpssState.grpCount = cfg->grpCount;

    printf("[INFO] Creating VPSS module with %u groups\n", cfg->grpCount);

    for (gi = 0; gi < (int)cfg->grpCount && gi < MAX_VPSS_GROUPS; gi++) {
        RockitVPSSGrpConfig *grp = &cfg->grpConfigs[gi];
        VPSS_GRP_ATTR_S stGrpAttr;

        /* 1. 创建 Group */
        rkos_memset(&stGrpAttr, 0, sizeof(stGrpAttr));
        stGrpAttr.u32MaxW = grp->maxWidth;
        stGrpAttr.u32MaxH = grp->maxHeight;
        stGrpAttr.enPixelFormat = RK_FMT_YUV420SP;
        stGrpAttr.stFrameRate.s32SrcFrameRate = -1;
        stGrpAttr.stFrameRate.s32DstFrameRate = -1;

        ret = RK_MPI_VPSS_CreateGrp(grp->grpId, &stGrpAttr);
        if (ret != RK_SUCCESS) {
            fprintf(stderr, "[ERROR] RK_MPI_VPSS_CreateGrp failed: grp=%d, %#x\n",
                    grp->grpId, ret);
            goto cleanup;
        }
        state->vpssState.grpEnabled[gi] = RKNS_TRUE;
        printf("[INFO]   VPSS group created: grp=%d, max=%ux%u\n",
               grp->grpId, grp->maxWidth, grp->maxHeight);

        /* 2. 设置视频处理设备 */
        ret = RK_MPI_VPSS_SetVProcDev(grp->grpId, VIDEO_PROC_DEV_RGA);
        if (ret != RK_SUCCESS) {
            fprintf(stderr, "[WARNING] RK_MPI_VPSS_SetVProcDev failed: grp=%d, %#x\n",
                    grp->grpId, ret);
        }

        /* 3. 重置 Group */
        ret = RK_MPI_VPSS_ResetGrp(grp->grpId);
        if (ret != RK_SUCCESS) {
            fprintf(stderr, "[ERROR] RK_MPI_VPSS_ResetGrp failed: grp=%d, %#x\n",
                    grp->grpId, ret);
            goto cleanup;
        }

        /* 4. 配置并使能通道 */
        state->vpssState.chnCount[gi] = grp->chnCount;

        for (ci = 0; ci < (int)grp->chnCount && ci < MAX_VPSS_CHNS; ci++) {
            RockitVPSSChnConfig *chn = &grp->chnConfigs[ci];
            VPSS_CHN_ATTR_S stVpssChnAttr;

            rkos_memset(&stVpssChnAttr, 0, sizeof(stVpssChnAttr));
            stVpssChnAttr.u32Width = chn->width;
            stVpssChnAttr.u32Height = chn->height;
            stVpssChnAttr.enPixelFormat = (PIXEL_FORMAT_E)chn->pixelFormat;
            stVpssChnAttr.u32Depth = chn->depth;

            ret = RK_MPI_VPSS_SetChnAttr(grp->grpId, chn->chnId, &stVpssChnAttr);
            if (ret != RK_SUCCESS) {
                fprintf(stderr, "[ERROR] RK_MPI_VPSS_SetChnAttr failed: grp=%d chn=%d, %#x\n",
                        grp->grpId, chn->chnId, ret);
                goto cleanup;
            }

            ret = RK_MPI_VPSS_EnableChn(grp->grpId, chn->chnId);
            if (ret != RK_SUCCESS) {
                fprintf(stderr, "[ERROR] RK_MPI_VPSS_EnableChn failed: grp=%d chn=%d, %#x\n",
                        grp->grpId, chn->chnId, ret);
                goto cleanup;
            }

            state->vpssState.chnEnabled[gi][ci] = RKNS_TRUE;
            printf("[INFO]   VPSS channel enabled: grp=%d, chn=%d, %ux%u, depth=%u, pixel_format=%s\n",
                   grp->grpId, chn->chnId, chn->width, chn->height, chn->depth,
                   vpss_pixel_format_to_string((PIXEL_FORMAT_E)chn->pixelFormat));
        }

        /* 5. 启动 Group */
        ret = RK_MPI_VPSS_StartGrp(grp->grpId);
        if (ret != RK_SUCCESS) {
            fprintf(stderr, "[ERROR] RK_MPI_VPSS_StartGrp failed: grp=%d, %#x\n",
                    grp->grpId, ret);
            goto cleanup;
        }
        printf("[INFO]   VPSS group started: grp=%d\n", grp->grpId);
    }

    if (cfg->grpCount > 0 && cfg->grpConfigs[0].chnCount > 0) {
        state->chn.enModId = (RKNS_MOD_ID_E)RK_ID_VPSS;
        state->chn.s32DevId = cfg->grpConfigs[0].grpId;
        state->chn.s32ChnId = cfg->grpConfigs[0].chnConfigs[0].chnId;
    }
    state->created = RKNS_TRUE;
    printf("[INFO] VPSS module created successfully\n");
    return RKNS_SUCCESS;

cleanup:
    for (int g = gi; g >= 0; g--) {
        RockitVPSSGrpConfig *cleanupGrp = &cfg->grpConfigs[g];
        /* 当前 group 尚未 StartGrp，只需清理通道 */
        int cEnd = (g == gi) ? ci - 1 : RK_MIN((int)cleanupGrp->chnCount, MAX_VPSS_CHNS) - 1;
        for (int c = cEnd; c >= 0; c--) {
            if (state->vpssState.chnEnabled[g][c]) {
                RK_MPI_VPSS_DisableChn(cleanupGrp->grpId, cleanupGrp->chnConfigs[c].chnId);
                state->vpssState.chnEnabled[g][c] = RKNS_FALSE;
            }
        }
        /* 已完成 StartGrp 的 group 需要先 Stop 再 Destroy */
        if (g < gi && state->vpssState.grpEnabled[g]) {
            RK_MPI_VPSS_StopGrp(cleanupGrp->grpId);
            RK_MPI_VPSS_DestroyGrp(cleanupGrp->grpId);
            state->vpssState.grpEnabled[g] = RKNS_FALSE;
        } else if (state->vpssState.grpEnabled[g]) {
            /* 当前失败的 group: 已 CreateGrp 但未 StartGrp */
            RK_MPI_VPSS_DestroyGrp(cleanupGrp->grpId);
            state->vpssState.grpEnabled[g] = RKNS_FALSE;
        }
    }
    return RKNS_FAILURE;
}

RKNS_S32 rockit_vpss_destroy(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    RockitVPSSConfig *cfg = MODULE_CONFIG(module, RockitVPSSConfig);
    int gi, ci;

    printf("[INFO] Destroying VPSS module\n");

    if (cfg->grpCount > 0 && cfg->grpConfigs != NULL) {
        for (gi = RK_MIN((int)cfg->grpCount, MAX_VPSS_GROUPS) - 1; gi >= 0; gi--) {
            RockitVPSSGrpConfig *grp = &cfg->grpConfigs[gi];

            /* 停止并销毁 Group */
            if (state->vpssState.grpEnabled[gi]) {
                /* 禁用所有通道 */
                for (ci = RK_MIN((int)grp->chnCount, MAX_VPSS_CHNS) - 1; ci >= 0; ci--) {
                    if (state->vpssState.chnEnabled[gi][ci]) {
                        RK_MPI_VPSS_DisableChn(grp->grpId, grp->chnConfigs[ci].chnId);
                        state->vpssState.chnEnabled[gi][ci] = RKNS_FALSE;
                    }
                }
                RK_MPI_VPSS_StopGrp(grp->grpId);
                RK_MPI_VPSS_DestroyGrp(grp->grpId);
                state->vpssState.grpEnabled[gi] = RKNS_FALSE;
            }
        }

        /* 释放所有动态分配的内存（遍历全部，不限于 MAX_VPSS_GROUPS） */
        for (gi = 0; gi < (int)cfg->grpCount; gi++) {
            if (cfg->grpConfigs[gi].chnConfigs != NULL) {
                rkos_kfree(cfg->grpConfigs[gi].chnConfigs);
                cfg->grpConfigs[gi].chnConfigs = NULL;
            }
        }
        rkos_kfree(cfg->grpConfigs);
        cfg->grpConfigs = NULL;
    }
    cfg->grpCount = 0;

    state->created = RKNS_FALSE;
    printf("[INFO] VPSS module destroyed\n");
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vpss_start(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    (void)module;
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_vpss_stop(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    (void)module;
    state->started = RKNS_FALSE;
    return RKNS_SUCCESS;
}

/* ---------- Rockit RGN 模块 ---------- */

RKNS_S32 rockit_rgn_parse_config(YamlDocument *doc, const char *base_path,
                                        RKNSLaunchModule *module) {
    RockitRGNConfig *cfg = (RockitRGNConfig *)module->configBuf;
    char path[256];
    const char *str;

    snprintf(path, sizeof(path), "%s.config.width", base_path);
    cfg->w = (unsigned int)yaml_get_int(doc, path, 640);

    snprintf(path, sizeof(path), "%s.config.height", base_path);
    cfg->h = (unsigned int)yaml_get_int(doc, path, 480);

    snprintf(path, sizeof(path), "%s.config.pixel_format", base_path);
    str = yaml_get_string(doc, path, "ARGB8888");
    if (strcmp(str, "RGB888") == 0) cfg->fmt = RK_FMT_RGB888;
    else if (strcmp(str, "RGBA8888") == 0) cfg->fmt = RK_FMT_RGBA8888;
    else if (strcmp(str, "BGRA8888") == 0) cfg->fmt = RK_FMT_BGRA8888;
    else if (strcmp(str, "ARGB8888") == 0) cfg->fmt = RK_FMT_ARGB8888;
    else cfg->fmt = RK_FMT_BGRA8888;

    /* Iterate attaches to find RGN-to-VO binding, default dev=7 chn=0 */
    cfg->vo_dev_id = 7;
    cfg->vo_chn_id = 0;
    {
        char ap[256];
        int pi, ai;
        if (sscanf(base_path, "pipelines.%d", &pi) == 1) {
            for (ai = 0; ai < 10; ai++) {
                snprintf(ap, sizeof(ap), "pipelines.%d.attaches.%d.name", pi, ai);
                const char *att_name = yaml_get_string(doc, ap, NULL);
                if (att_name && strstr(att_name, "rgn") != NULL) {
                    snprintf(ap, sizeof(ap), "pipelines.%d.attaches.%d.dst_dev_id", pi, ai);
                    cfg->vo_dev_id = yaml_get_int(doc, ap, 7);
                    snprintf(ap, sizeof(ap), "pipelines.%d.attaches.%d.dst_chn_id", pi, ai);
                    cfg->vo_chn_id = yaml_get_int(doc, ap, 0);
                    break;
                }
            }
        }
    }
    printf("[INFO] RGN Module Config:\n");
    printf("[INFO]   chn_id: %d\n", module->chnId);
    printf("[INFO]   width: %u, height: %u, fmt: %s(%u)\n",
           cfg->w, cfg->h, str, cfg->fmt);

    module->configParsed = RKNS_TRUE;
    return RKNS_SUCCESS;
}

/* Inline RGN operations (was rockit_rgn.c) */
#include "rk_mpi_rgn.h"


static int rgn_update(int rgnHandle, const BITMAP_S *bitmap)
{
    return RK_MPI_RGN_SetBitMap((RGN_HANDLE)rgnHandle, bitmap);
}

static int rgn_create(const void *config, int devId, int chnId)
{
    const RockitRGNConfig *cfg = (RockitRGNConfig *)config;
    RGN_HANDLE handle = (RGN_HANDLE)chnId;
    RGN_ATTR_S stRgnAttr;
    int ret;
    (void)devId;
    printf("[INFO] Creating RGN: handle=%d, %ux%u fmt=%u\n", handle, cfg->w, cfg->h, cfg->fmt);
    memset(&stRgnAttr, 0, sizeof(stRgnAttr));
    stRgnAttr.enType = OVERLAY_RGN;
    stRgnAttr.unAttr.stOverlay.enPixelFmt = (PIXEL_FORMAT_E)cfg->fmt;
    stRgnAttr.unAttr.stOverlay.u32CanvasNum = 1;
    stRgnAttr.unAttr.stOverlay.stSize.u32Width = cfg->w;
    stRgnAttr.unAttr.stOverlay.stSize.u32Height = cfg->h;
    ret = RK_MPI_RGN_Create(handle, &stRgnAttr);
    if (ret != 0) { fprintf(stderr, "[ERROR] RGN_Create(%d): %#x\n", handle, ret); return -1; }
    printf("[INFO] RGN handle %d created OK\n", handle);
    return 0;
}

static void rgn_destroy(int chnId)
{
    RK_MPI_RGN_Destroy((RGN_HANDLE)chnId);
}

static void rgn_detach(int rgnHandle, int modId, int devId, int chnId)
{
    MPP_CHN_S stChn;
    memset(&stChn, 0, sizeof(stChn));
    stChn.enModId = (MOD_ID_E)modId;
    stChn.s32DevId = devId;
    stChn.s32ChnId = chnId;
    RK_MPI_RGN_DetachFromChn((RGN_HANDLE)rgnHandle, &stChn);
}


static int rgn_attach(int rgnHandle, int modId, int devId, int chnId)
{
    MPP_CHN_S stChn;
    RGN_CHN_ATTR_S stChnAttr;
    int ret;
    memset(&stChn, 0, sizeof(stChn));
    stChn.enModId = (MOD_ID_E)modId;
    stChn.s32DevId = devId;
    stChn.s32ChnId = chnId;
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.bShow = RK_TRUE;
    stChnAttr.enType = OVERLAY_RGN;
    stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = 0;
    stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = 0;
    stChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 0;
    stChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 0;
    stChnAttr.unChnAttr.stOverlayChn.u32Layer = 0;
    stChnAttr.unChnAttr.stOverlayChn.stQpInfo.bEnable = RK_FALSE;
    stChnAttr.unChnAttr.stOverlayChn.stQpInfo.bForceIntra = RK_TRUE;
    stChnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = RK_FALSE;
    stChnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp = 0;
    stChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[0] = 0x00;
    stChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[1] = 0xFFFFFF;
    stChnAttr.unChnAttr.stOverlayChn.stInvertColor.bInvColEn = RK_FALSE;
    stChnAttr.unChnAttr.stOverlayChn.stInvertColor.stInvColArea.u32Width = 16;
    stChnAttr.unChnAttr.stOverlayChn.stInvertColor.stInvColArea.u32Height = 16;
    stChnAttr.unChnAttr.stOverlayChn.stInvertColor.enChgMod = LESSTHAN_LUM_THRESH;
    stChnAttr.unChnAttr.stOverlayChn.stInvertColor.u32LumThresh = 100;
    ret = RK_MPI_RGN_AttachToChn((RGN_HANDLE)rgnHandle, &stChn, &stChnAttr);
    if (ret != 0) { fprintf(stderr, "[ERROR] RGN_AttachToChn(%d): %#x\n", rgnHandle, ret); return ret; }
    printf("[INFO] RGN %d attached to (mod=%d,dev=%d,chn=%d)\n", rgnHandle, modId, devId, chnId);
    return 0;
}

RKNS_S32 rockit_rgn_create(const RKNSLaunchModule *module,
                                 RKNSLaunchModuleState *state) {
    printf("[INFO] Creating RGN module: devId=%d, chnId=%d\n",
           module->devId, module->chnId);

    state->chn.enModId = (RKNS_MOD_ID_E)RK_ID_RGN;
    state->chn.s32DevId = module->devId;
    state->chn.s32ChnId = module->chnId;
    state->created = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_rgn_destroy(const RKNSLaunchModule *module,
                                  RKNSLaunchModuleState *state) {
    if (state->created) {
        printf("[INFO] Destroying RGN module: devId=%d, chnId=%d\n",
               module->devId, module->chnId);
        rgn_destroy(module->chnId);
        state->created = RKNS_FALSE;
    }
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_rgn_start(const RKNSLaunchModule *module,
                                RKNSLaunchModuleState *state) {
    (void)module;
    /* Create and attach RGN after VO started */
    {
        RockitRGNConfig *cfg = (RockitRGNConfig *)module->configBuf;
        struct { unsigned int w, h, fmt; } rgnCfg = { cfg->w, cfg->h, cfg->fmt };
        int ret = rgn_create(&rgnCfg, 0, module->chnId);
        if (ret == 0) {
            ret = rgn_attach(module->chnId, RK_ID_VO, cfg->vo_dev_id, cfg->vo_chn_id);
            if (ret != 0) {
                fprintf(stderr, "[ERROR] RGN attach failed: %d\n", ret);
                rgn_destroy(module->chnId);
                state->created = RKNS_FALSE;
                return RKNS_FAILURE;
            }
            else
                printf("[INFO] RGN handle=%d attached to VO\n", (int)module->chnId);
        } else {
            fprintf(stderr, "[ERROR] RGN create failed: %d\n", ret);
            state->created = RKNS_FALSE;
            return RKNS_FAILURE;
        }
    }
    state->started = RKNS_TRUE;
    return RKNS_SUCCESS;
}

RKNS_S32 rockit_rgn_stop(const RKNSLaunchModule *module,
                               RKNSLaunchModuleState *state) {
    RockitRGNConfig *cfg = (RockitRGNConfig *)module->configBuf;
    rgn_detach(module->chnId, RK_ID_VO, cfg->vo_dev_id, cfg->vo_chn_id);
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
    /* RKNS 模块 - 直接用原始 ID (RKNS_ID_XXX) */
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
    { RKNS_ID_TEXT,     "TEXT",     text_parse_config,
      text_create,     text_destroy,     text_start,     text_stop     },
    /* Rockit 模块 - 原始 ID (RK_ID_XXX) + 偏移 */
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
    return (id >= RKNS_LAUNCH_ROCKIT_ID_OFFSET) ? RKNS_TRUE : RKNS_FALSE;
}

RKNS_S32 rkns_launch_to_mpp_id(RKNSLaunchModId id) {
    return (id >= RKNS_LAUNCH_ROCKIT_ID_OFFSET) ? (id - RKNS_LAUNCH_ROCKIT_ID_OFFSET) : -1;
}

RKNS_S32 rkns_launch_to_rkns_id(RKNSLaunchModId id) {
    return (id < RKNS_LAUNCH_ROCKIT_ID_OFFSET && id >= 0) ? (RKNS_S32)id : -1;
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

/**
 * @brief rknnstream launch ui
 */
static void rknnstream_launch_ui(void) {
    struct timespec start_time, current_time;
    int fd = STDIN_FILENO;
    long elapsed_seconds = 0;
    RKNS_S32 ret;

    // 输入缓冲区
    char input_buf[1024] = {0};
    int input_pos = 0;

    // 获取开始时间
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // 直接使用配置文件中的 UI 交互配置
    int send_dev_id = g_config.uiConfig.sendDevId;
    int send_chn_id = g_config.uiConfig.sendChnId;
    int recv_dev_id = g_config.uiConfig.recvDevId;
    int recv_chn_id = g_config.uiConfig.recvChnId;
    // 清除屏幕并移动光标到左上角
    clear_screen();
    
    // 初始绘制聊天界面
    redraw_chat_interface(input_buf, input_pos);

    char last_char = 0;
    
    // 主循环
    while (g_running && !g_exit) {
        // 检查超时
        if (g_config.timeout > 0) {
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            elapsed_seconds = current_time.tv_sec - start_time.tv_sec;

            if (elapsed_seconds >= g_config.timeout) {
                break;
            }
        }

        // 检查用户输入
        char c;
        int key_ret = read(fd, &c, 1);
        if (key_ret > 0) {
            // 避免重复处理回车换行（\r 和 \n 经常一起出现）
            if ((c == '\n' && last_char == '\r') || (c == '\r' && last_char == '\n')) {
                last_char = c;
                continue;
            }
            last_char = c;
            
            if (c == 'q' || c == 'Q') {
                // 退出程序
                break;
            } else if (c == '\n' || c == '\r') {
                // 发送输入
                if (input_pos > 0) {
                    input_buf[input_pos] = '\0';
                    
                    // 添加用户消息到聊天历史
                    add_chat_message(MSG_TYPE_USER, input_buf);
                    redraw_chat_interface("", 0); // 全屏重绘

                    // 发送到配置的发送模块
                    if (send_dev_id >= 0 && send_chn_id >= 0) {
                        RKNS_HANDLE hInBuf = 0;
                        if (create_string_value_frame(input_buf, &hInBuf) == 0) {
                            ret = RKNS_INPUT_SendFrame(send_dev_id, send_chn_id, hInBuf);
                            if (ret != RKNS_SUCCESS) {
                                // 发送失败，不显示
                            } else {
                                // 等待配置的接收模块的回复
                                RKNS_HANDLE hOutBuf = 0;
                                ret = RKNS_OUTPUT_GetFrame(recv_dev_id, recv_chn_id, &hOutBuf, 600000); // 10分钟超时
                                if (ret == RKNS_SUCCESS && hOutBuf != 0) {
                                    RKNSMemFrame out_info;
                                    if (RKNS_MEM_GetBufInfo(hOutBuf, &out_info) == RKNS_SUCCESS) {
                                        const char *llm_output = out_info.value.s ? out_info.value.s : "";
                                        // 添加LLM回复到聊天历史
                                        add_chat_message(MSG_TYPE_LLM, llm_output);
                                    }
                                    RKNS_OUTPUT_ReleaseFrame(recv_dev_id, recv_chn_id, hOutBuf);
                                }
                                // 重新绘制界面显示新消息
                                redraw_chat_interface("", 0);
                            }
                            RKNS_MEM_DestroyFrame(hInBuf);
                        }
                    }

                    // 重置缓冲区
                    input_pos = 0;
                    memset(input_buf, 0, sizeof(input_buf));
                }
            } else if (c == '\b' || c == 127) {
                // 退格键 - 只更新输入行，不刷屏
                if (input_pos > 0) {
                    input_pos--;
                    input_buf[input_pos] = '\0';
                    // 清除当前行并重新打印
                    printf("\r\033[2K> %s", input_buf);
                    fflush(stdout);
                }
            } else if (input_pos < sizeof(input_buf) - 1) {
                // 普通字符 - 只更新输入行，不刷屏
                input_buf[input_pos++] = c;
                printf("%c", c);
                fflush(stdout);
            }
        } else if (key_ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // 读错误
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Read error: %s", strerror(errno));
            add_chat_message(MSG_TYPE_SYSTEM, err_msg);
            redraw_chat_interface(input_buf, input_pos);
            break;
        }

        // 短暂休眠避免 CPU 占用过高
        usleep(10000);  // 10ms，响应更快
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

    // 排序：Rockit pipeline 优先创建（RKNNStream INPUT 可能依赖 Rockit VPSS）
    for (i = 0; i < (RKNS_S32)pipelineCount - 1; i++) {
        RKNS_S32 j;
        for (j = i + 1; j < (RKNS_S32)pipelineCount; j++) {
            if (pipelineContexts[j].desc->type == RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT &&
                pipelineContexts[i].desc->type != RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT) {
                RKNSLaunchPipelineContext tmp = pipelineContexts[i];
                pipelineContexts[i] = pipelineContexts[j];
                pipelineContexts[j] = tmp;
            }
        }
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

    // 保存全局变量供后续使用
    g_pipeline_contexts = pipelineContexts;
    g_pipeline_count = pipelineCount;

    // 设置终端为非阻塞模式
    set_nonblocking(fd);

    // 创建所有 pipeline（Rockit 优先）
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

    // 检查是否启用 UI 交互
    if (g_config.uiConfig.enabled) {
        rknnstream_launch_ui();
    } else {
        // UI 交互未启用，简单等待退出信号或超时
        printf("[INFO] UI interaction disabled. Press Ctrl+C to exit.\n");
        
        // 获取开始时间
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        while (g_running && !g_exit) {
            // 检查超时
            if (g_config.timeout > 0) {
                clock_gettime(CLOCK_MONOTONIC, &current_time);
                elapsed_seconds = current_time.tv_sec - start_time.tv_sec;

                if (elapsed_seconds >= g_config.timeout) {
                    break;
                }
            }

            // 检查用户输入（简单的 q 退出）
            char c;
            int key_ret = read(fd, &c, 1);
            if (key_ret > 0 && (c == 'q' || c == 'Q')) {
                // 退出程序
                break;
            }

            // 短暂休眠避免 CPU 占用过高
            usleep(100000);  // 100ms
        }
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
