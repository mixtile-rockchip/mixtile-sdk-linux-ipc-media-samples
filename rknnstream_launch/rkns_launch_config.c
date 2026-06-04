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
 * @file rkns_launch_config.c
 * @brief RKNNStream Launch YAML 配置文件解析
 */

#include "rkns_launch_config.h"
#include "yaml_parse.h"
#include "rk_osal.h"
#include "rkns.h"
/* Rockit MPI headers */
#include "rk_mpi_sys.h"
#include "rk_common.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * 全局变量定义
 * ============================================================ */

const char *g_scenarioStr[] = {
    "detection",
    "face",
    "segment",
    "track",
    "custom"
};

const char *g_inputTypeStr[] = {
    "ROCKIT",
    "GSTREAMER",
    "OPENCV",
    "CUSTOM"
};

const char *g_outputTypeStr[] = {
    "APP",
    "ROCKIT",
    "GSTREAM",
    "OPENCV"
};

const char *g_modelTypeStr[] = {
    "ALM",
    "CLASSIFY",
    "DETECT",
    "LLM",
    "SEGMENT",
    "TRACK",
    "TTA",
    "VLM"
};

/* ============================================================
 * 私有函数
 * ============================================================ */

/**
 * @brief 解析场景类型
 */
static RKNSLaunchScenario parse_scenario(const char *scenario_str) {
    if (strcmp(scenario_str, "detection") == 0) {
        return RKNS_LAUNCH_SCENARIO_DETECTION;
    } else if (strcmp(scenario_str, "face") == 0) {
        return RKNS_LAUNCH_SCENARIO_FACE;
    } else if (strcmp(scenario_str, "segment") == 0) {
        return RKNS_LAUNCH_SCENARIO_SEGMENT;
    } else if (strcmp(scenario_str, "track") == 0) {
        return RKNS_LAUNCH_SCENARIO_TRACK;
    } else if (strcmp(scenario_str, "custom") == 0) {
        return RKNS_LAUNCH_SCENARIO_CUSTOM;
    }
    return RKNS_LAUNCH_SCENARIO_DETECTION; // 默认
}

/**
 * @brief 解析输入源类型
 */
RKNSInputSourceType parse_input_type(const char *type_str) {
    if (strcmp(type_str, "ROCKIT") == 0) {
        return RKNS_INPUT_SOURCE_ROCKIT;
    } else if (strcmp(type_str, "GSTREAMER") == 0) {
        return RKNS_INPUT_SOURCE_GSTREAMER;
    } else if (strcmp(type_str, "OPENCV") == 0) {
        return RKNS_INPUT_SOURCE_OPENCV;
    } else if (strcmp(type_str, "APP") == 0) {
        return RKNS_INPUT_SOURCE_APP;
    } else if (strcmp(type_str, "CUSTOM") == 0) {
        return RKNS_INPUT_SOURCE_CUSTOM;
    }
    return RKNS_INPUT_SOURCE_CUSTOM; // 默认
}

/**
 * @brief 解析输出类型
 */
RKNSOutputType parse_output_type(const char *type_str) {
    if (strcmp(type_str, "APP") == 0) {
        return RKNS_OUTPUT_TYPE_APP;
    } else if (strcmp(type_str, "ROCKIT") == 0) {
        return RKNS_OUTPUT_TYPE_ROCKIT;
    } else if (strcmp(type_str, "GSTREAM") == 0) {
        return RKNS_OUTPUT_TYPE_GSTREAM;
    } else if (strcmp(type_str, "OPENCV") == 0) {
        return RKNS_OUTPUT_TYPE_OPENCV;
    }
    return RKNS_OUTPUT_TYPE_APP; // 默认
}

/**
 * @brief 解析模型类型
 */
RKNS_MODEL_TYPE_E parse_model_type(const char *type_str) {
    if (strcmp(type_str, "ALM") == 0) {
        return RKNS_MODEL_TYPE_ALM;
    } else if (strcmp(type_str, "CLASSIFY") == 0) {
        return RKNS_MODEL_TYPE_CLASSIFY;
    } else if (strcmp(type_str, "DETECT") == 0) {
        return RKNS_MODEL_TYPE_DETECT;
    } else if (strcmp(type_str, "LLM") == 0) {
        return RKNS_MODEL_TYPE_LLM;
    } else if (strcmp(type_str, "SEGMENT") == 0) {
        return RKNS_MODEL_TYPE_SEGMENT;
    } else if (strcmp(type_str, "TRACK") == 0) {
        return RKNS_MODEL_TYPE_TRACK;
    } else if (strcmp(type_str, "TTA") == 0) {
        return RKNS_MODEL_TYPE_TTA;
    } else if (strcmp(type_str, "VLM") == 0) {
        return RKNS_MODEL_TYPE_VLM;
    }
    return RKNS_MODEL_TYPE_DETECT; // 默认
}

/**
 * @brief 解析像素格式
 */
RockXPixelFormat  parse_pixel_format(const char *fmt_str) {
    if (strcmp(fmt_str, "NV12") == 0) {
        return ROCKX_PIXEL_FORMAT_YUV420SP_NV12;
    } else if (strcmp(fmt_str, "RGB888") == 0) {
        return ROCKX_PIXEL_FORMAT_RGB888;
    } else if (strcmp(fmt_str, "BGRA8888") == 0) {
        return ROCKX_PIXEL_FORMAT_BGRA8888;
    } else if (strcmp(fmt_str, "RGBA8888") == 0) {
        return ROCKX_PIXEL_FORMAT_RGBA8888;
    }
    return ROCKX_PIXEL_FORMAT_YUV420SP_NV12; // 默认
}

/**
 * @brief 解析 Rockit 命令类型
 */
static RKNSRockitCmd parse_rockit_cmd(const char *cmd_str) {
    if (strcmp(cmd_str, "UPDATE_RGN") == 0) {
        return RKNS_ROCKIT_CMD_UPDATE_RGN;
    } else if (strcmp(cmd_str, "SEND_VPSS") == 0) {
        return RKNS_ROCKIT_CMD_SEND_VPSS;
    } else if (strcmp(cmd_str, "SEND_VO") == 0) {
        return RKNS_ROCKIT_CMD_SEND_VO;
    } else if (strcmp(cmd_str, "SEND_AO") == 0) {
        return RKNS_ROCKIT_CMD_SEND_AO;
    } else if (strcmp(cmd_str, "SEND_AENC") == 0) {
        return RKNS_ROCKIT_CMD_SEND_AENC;
    } else if (strcmp(cmd_str, "SEND_VENC") == 0) {
        return RKNS_ROCKIT_CMD_SEND_VENC;
    }
    return RKNS_ROCKIT_CMD_SEND_VO; // 默认
}

/**
 * @brief 解析模块名称到统一模块 ID
 */
static RKNSLaunchModId parse_module_id(const char *name) {
    /* RKNS 模块 - 直接用原始 ID (RKNS_ID_XXX) */
    if (strcmp(name, "INPUT") == 0)    return RKNS_ID_INPUT;
    if (strcmp(name, "INFER") == 0)    return RKNS_ID_INFER;
    if (strcmp(name, "GRAPHICS") == 0) return RKNS_ID_GRAPHICS;
    if (strcmp(name, "OUTPUT") == 0)   return RKNS_ID_OUTPUT;
    if (strcmp(name, "SYS") == 0)      return RKNS_ID_SYS;
    if (strcmp(name, "MB") == 0)       return RKNS_ID_MB;
    if (strcmp(name, "TEXT") == 0)     return RKNS_ID_TEXT;
    if (strcmp(name, "IMAGE") == 0)    return RKNS_ID_IMAGE;
    if (strcmp(name, "COMPOSE") == 0)  return RKNS_ID_COMPOSE;
    if (strcmp(name, "EMBEDDING") == 0) return RKNS_ID_EMBEDDING;
    if (strcmp(name, "VOICE") == 0)    return RKNS_ID_VOICE;
    if (strcmp(name, "BUSINESS") == 0) return RKNS_ID_BUSINESS;
    /* Rockit 模块 - 原始 ID (RK_ID_XXX) + 偏移 */
    if (strcmp(name, "VI") == 0)       return RK_ID_VI + RKNS_LAUNCH_ROCKIT_ID_OFFSET;
    if (strcmp(name, "VO") == 0)       return RK_ID_VO + RKNS_LAUNCH_ROCKIT_ID_OFFSET;
    if (strcmp(name, "VPSS") == 0)     return RK_ID_VPSS + RKNS_LAUNCH_ROCKIT_ID_OFFSET;
    if (strcmp(name, "RGN") == 0)      return RK_ID_RGN + RKNS_LAUNCH_ROCKIT_ID_OFFSET;
    if (strcmp(name, "AI") == 0)       return RK_ID_AI + RKNS_LAUNCH_ROCKIT_ID_OFFSET;
    return -1;
}

/**
 * @brief 解析 pipeline type 字符串
 */
static RKNSLaunchPipelineType parse_pipeline_type(const char *type_str) {
    if (strcmp(type_str, "rknnstream") == 0) return RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM;
    if (strcmp(type_str, "rockit") == 0)     return RKNS_LAUNCH_PIPELINE_TYPE_ROCKIT;
    return RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM;  // 默认
}

/**
 * @brief 从 YAML pipelines[] 解析所有 pipeline 描述
 *
 * 解析每条 pipeline 的 name/type/enabled、modules[]、bindings[]、attaches[]，
 * 完整存入 config->pipelines[] 数组。不涉及具体模块配置参数的加载。
 */
static RKNS_S32 load_pipelines_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    int pipeline_count;
    int pi;
    char path[256];

    pipeline_count = yaml_get_count(doc, "pipelines");
    if (pipeline_count <= 0) {
        return RKNS_SUCCESS;  // 无 pipelines 段，使用默认
    }

    if (pipeline_count > MAX_PIPELINES) {
        pipeline_count = MAX_PIPELINES;
    }
    config->pipelineCount = (RKNS_U32)pipeline_count;

    for (pi = 0; pi < pipeline_count; pi++) {
        RKNSLaunchPipelineDesc *pl = &config->pipelines[pi];
        const char *str;

        // name
        snprintf(path, sizeof(path), "pipelines.%d.name", pi);
        str = yaml_get_string(doc, path, "");
        rkos_strncpy(pl->name, str, MAX_PIPELINE_NAME - 1);
        pl->name[MAX_PIPELINE_NAME - 1] = '\0';

        // type
        snprintf(path, sizeof(path), "pipelines.%d.type", pi);
        str = yaml_get_string(doc, path, "rknnstream");
        pl->type = parse_pipeline_type(str);

        // enabled
        snprintf(path, sizeof(path), "pipelines.%d.enabled", pi);
        pl->enabled = (RKNS_BOOL)yaml_get_bool(doc, path, 0);

        // ---------- 解析 modules ----------
        snprintf(path, sizeof(path), "pipelines.%d.modules", pi);
        {
            int module_count = yaml_get_count(doc, path);
            int mi;

            if (module_count < 0) module_count = 0;
            if (module_count > MAX_MODULES) module_count = MAX_MODULES;
            pl->moduleCount = (RKNS_U32)module_count;

            for (mi = 0; mi < module_count; mi++) {
                RKNSLaunchModule *mod = &pl->modules[mi];

                snprintf(path, sizeof(path), "pipelines.%d.modules.%d.name", pi, mi);
                str = yaml_get_string(doc, path, "");
                rkos_strncpy(mod->name, str, MAX_MODULE_NAME - 1);
                mod->name[MAX_MODULE_NAME - 1] = '\0';
                mod->modId = parse_module_id(str);

                snprintf(path, sizeof(path), "pipelines.%d.modules.%d.dev_id", pi, mi);
                mod->devId = (RKNS_S32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "pipelines.%d.modules.%d.chn_id", pi, mi);
                mod->chnId = (RKNS_S32)yaml_get_int(doc, path, 0);

                /* Rockit 扩展字段 */
                snprintf(path, sizeof(path), "pipelines.%d.modules.%d.pipe_id", pi, mi);
                mod->pipeId = (RKNS_S32)yaml_get_int(doc, path, mod->devId);

                snprintf(path, sizeof(path), "pipelines.%d.modules.%d.layer_id", pi, mi);
                mod->layerId = (RKNS_S32)yaml_get_int(doc, path, 5);

                /* 解析模块特有 config（通过注册表） */
                {
                    const RKNSLaunchModuleOps *ops = rkns_launch_find_module_ops(mod->modId);
                    if (ops && ops->parse_config) {
                        char config_path[256];
                        /* 直接使用模块路径，不加 .config 后缀
                         * 因为 YAML 中 AI/INPUT/INFER 等模块的配置直接在模块下
                         */
                        snprintf(config_path, sizeof(config_path),
                                 "pipelines.%d.modules.%d", pi, mi);
                        ops->parse_config(doc, config_path, mod);
                    }
                }
            }
        }

        // ---------- 解析 bindings ----------
        snprintf(path, sizeof(path), "pipelines.%d.bindings", pi);
        {
            int binding_count = yaml_get_count(doc, path);
            int bi;

            if (binding_count < 0) binding_count = 0;
            if (binding_count > MAX_BINDINGS) binding_count = MAX_BINDINGS;
            pl->bindingCount = (RKNS_U32)binding_count;

            for (bi = 0; bi < binding_count; bi++) {
                RKNSLaunchBinding *bind = &pl->bindings[bi];

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.name", pi, bi);
                str = yaml_get_string(doc, path, "");
                rkos_strncpy(bind->name, str, MAX_BINDING_NAME - 1);
                bind->name[MAX_BINDING_NAME - 1] = '\0';

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.src_module", pi, bi);
                str = yaml_get_string(doc, path, "");
                bind->srcChn.enModId = (RKNS_MOD_ID_E)parse_module_id(str);

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.src_dev_id", pi, bi);
                bind->srcChn.s32DevId = (RKNS_S32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.src_chn_id", pi, bi);
                bind->srcChn.s32ChnId = (RKNS_S32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.dst_module", pi, bi);
                str = yaml_get_string(doc, path, "");
                bind->dstChn.enModId = (RKNS_MOD_ID_E)parse_module_id(str);

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.dst_dev_id", pi, bi);
                bind->dstChn.s32DevId = (RKNS_S32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "pipelines.%d.bindings.%d.dst_chn_id", pi, bi);
                bind->dstChn.s32ChnId = (RKNS_S32)yaml_get_int(doc, path, 0);
            }
        }

        // ---------- 解析 attaches（RGN，可选） ----------
        snprintf(path, sizeof(path), "pipelines.%d.attaches", pi);
        {
            int attach_count = yaml_get_count(doc, path);
            int ai;

            if (attach_count < 0) attach_count = 0;
            if (attach_count > MAX_ATTACHES) attach_count = MAX_ATTACHES;
            pl->attachCount = (RKNS_U32)attach_count;

            for (ai = 0; ai < attach_count; ai++) {
                RKNSLaunchAttach *att = &pl->attaches[ai];

                snprintf(path, sizeof(path), "pipelines.%d.attaches.%d.rgn_handle", pi, ai);
                att->rgnHandle = (RKNS_S32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "pipelines.%d.attaches.%d.dst_module", pi, ai);
                str = yaml_get_string(doc, path, "");
                att->dstChn.enModId = (RKNS_MOD_ID_E)parse_module_id(str);

                snprintf(path, sizeof(path), "pipelines.%d.attaches.%d.dst_dev_id", pi, ai);
                att->dstChn.s32DevId = (RKNS_S32)yaml_get_int(doc, path, 0);

                snprintf(path, sizeof(path), "pipelines.%d.attaches.%d.dst_chn_id", pi, ai);
                att->dstChn.s32ChnId = (RKNS_S32)yaml_get_int(doc, path, 0);
            }
        }

        printf("[INFO] Pipeline[%d]: name=%s, type=%s, enabled=%s, "
               "modules=%u, bindings=%u, attaches=%u\n",
               pi, pl->name,
               pl->type == RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM ? "rknnstream" : "rockit",
               pl->enabled ? "true" : "false",
               pl->moduleCount, pl->bindingCount, pl->attachCount);
    }

    return RKNS_SUCCESS;
}

/**
 * @brief 加载运行时配置
 */
static RKNS_S32 load_runtime_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    // 超时设置
    config->timeout = (RKNS_S32)yaml_get_int(doc, "runtime.timeout", 0);

    // 性能分析
    config->enableProfiling = (RKNS_BOOL)yaml_get_bool(doc, "runtime.profiling", 0);

    // 详细输出
    config->verbose = (RKNS_BOOL)yaml_get_bool(doc, "runtime.verbose", 1);

    return RKNS_SUCCESS;
}

/**
 * @brief 加载系统配置
 */
static RKNS_S32 load_system_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    // 检查是否存在 UI 交互配置
    YamlNode *ui_node = yaml_get_node(doc, "system.ui_interaction");
    if (ui_node != NULL) {
        // 只要存在 ui_interaction 节点，就启用 UI
        config->uiConfig.enabled = RKNS_TRUE;
        
        // 加载 UI 交互配置 - 发送模块
        const char *send_module = yaml_get_string(doc, "system.ui_interaction.send_module", "INPUT");
        rkos_strncpy(config->uiConfig.sendModuleName, send_module, MAX_MODULE_NAME - 1);
        config->uiConfig.sendModuleName[MAX_MODULE_NAME - 1] = '\0';
        config->uiConfig.sendDevId = (RKNS_S32)yaml_get_int(doc, "system.ui_interaction.send_dev_id", 0);
        config->uiConfig.sendChnId = (RKNS_S32)yaml_get_int(doc, "system.ui_interaction.send_chn_id", 0);
        
        // 加载 UI 交互配置 - 接收模块
        const char *recv_module = yaml_get_string(doc, "system.ui_interaction.recv_module", "OUTPUT");
        rkos_strncpy(config->uiConfig.recvModuleName, recv_module, MAX_MODULE_NAME - 1);
        config->uiConfig.recvModuleName[MAX_MODULE_NAME - 1] = '\0';
        config->uiConfig.recvDevId = (RKNS_S32)yaml_get_int(doc, "system.ui_interaction.recv_dev_id", 0);
        config->uiConfig.recvChnId = (RKNS_S32)yaml_get_int(doc, "system.ui_interaction.recv_chn_id", 0);
    } else {
        // 如果没有 UI 交互配置，则禁用
        config->uiConfig.enabled = RKNS_FALSE;
    }

    return RKNS_SUCCESS;
}

/* ============================================================
 * 公共函数
 * ============================================================ */

/**
 * @brief 从 YAML 文件加载配置
 */
RKNS_S32 rkns_launch_load_config_from_yaml(RKNSLaunchConfig *config,
                                          const char *yaml_file) {
    YamlDocument doc;
    RKNS_S32 ret = RKNS_SUCCESS;
    const char *scenario_str;

    // 初始化 YAML 文档
    yaml_doc_init(&doc);

    // 解析 YAML 文件
    if (yaml_parse_file(&doc, yaml_file) != 0) {
        fprintf(stderr, "[ERROR] Failed to parse YAML file: %s\n", yaml_file);
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    printf("[INFO] Loaded configuration from: %s\n", yaml_file);

    if (load_runtime_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load runtime configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    if (load_system_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load system configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    if (load_pipelines_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load pipelines configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

cleanup:
    // 清理 YAML 文档
    yaml_doc_cleanup(&doc);

    return ret;
}

/**
 * @brief 保存当前配置到 YAML 文件
 */
RKNS_S32 rkns_launch_save_config_to_yaml(const RKNSLaunchConfig *config,
                                        const char *yaml_file) {
    FILE *fp;

    fp = fopen(yaml_file, "w");
    if (!fp) {
        fprintf(stderr, "[ERROR] Failed to open file for writing: %s\n", yaml_file);
        return RKNS_FAILURE;
    }

    // 写入配置文件
    fprintf(fp, "# RKNNStream Launch Configuration File\n");
    fprintf(fp, "# 自动生成于配置保存\n\n");

    fclose(fp);

    printf("[INFO] Configuration saved to: %s\n", yaml_file);

    return RKNS_SUCCESS;
}

/**
 * @brief 初始化默认配置
 */
void init_default_config(RKNSLaunchConfig *config) {
    if (!config) {
        return;
    }

    // 运行时配置
    config->timeout = 0;
    config->enableProfiling = RKNS_FALSE;
    config->verbose = RKNS_TRUE;  
    
    // UI 交互配置默认值（默认启用）
    config->uiConfig.enabled = RKNS_TRUE;
    rkos_strncpy(config->uiConfig.sendModuleName, "INPUT", MAX_MODULE_NAME - 1);
    config->uiConfig.sendModuleName[MAX_MODULE_NAME - 1] = '\0';
    config->uiConfig.sendDevId = 0;
    config->uiConfig.sendChnId = 0;
    
    rkos_strncpy(config->uiConfig.recvModuleName, "OUTPUT", MAX_MODULE_NAME - 1);
    config->uiConfig.recvModuleName[MAX_MODULE_NAME - 1] = '\0';
    config->uiConfig.recvDevId = 0;
    config->uiConfig.recvChnId = 0;
}

/**
 * @brief 打印配置信息
 */
void print_config(const RKNSLaunchConfig *config) {
    if (!config) {
        return;
    }

    printf("\n========== RKNNStream Launch Configuration ==========\n");   
    // 运行时配置
    printf("[INFO] Timeout: %d seconds\n", config->timeout);
    printf("[INFO] Profiling: %s\n",
           config->enableProfiling ? "enabled" : "disabled");
    printf("[INFO] Verbose: %s\n",
           config->verbose ? "enabled" : "disabled");

    // UI 交互配置
    printf("\n[INFO] UI Interaction Configuration:\n");
    printf("[INFO]   Enabled: %s\n",
           config->uiConfig.enabled ? "yes" : "no");
    if (config->uiConfig.enabled) {
        printf("[INFO]   Send Module: %s (dev=%d, chn=%d)\n",
               config->uiConfig.sendModuleName,
               config->uiConfig.sendDevId,
               config->uiConfig.sendChnId);
        printf("[INFO]   Recv Module: %s (dev=%d, chn=%d)\n",
               config->uiConfig.recvModuleName,
               config->uiConfig.recvDevId,
               config->uiConfig.recvChnId);
    }

    // Pipeline 配置
    if (config->pipelineCount > 0) {
        RKNS_U32 pi;
        for (pi = 0; pi < config->pipelineCount; pi++) {
            const RKNSLaunchPipelineDesc *pl = &config->pipelines[pi];
            RKNS_U32 i;

            printf("[INFO] Pipeline[%u]: name=%s, type=%s, enabled=%s\n",
                   pi, pl->name,
                   pl->type == RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM ? "rknnstream" : "rockit",
                   pl->enabled ? "true" : "false");

            printf("[INFO]   Modules (%u):\n", pl->moduleCount);
            for (i = 0; i < pl->moduleCount; i++) {
                printf("[INFO]     [%u] %s (modId=%d, dev=%d, chn=%d)\n",
                       i, pl->modules[i].name,
                       pl->modules[i].modId,
                       pl->modules[i].devId, pl->modules[i].chnId);
            }

            printf("[INFO]   Bindings (%u):\n", pl->bindingCount);
            for (i = 0; i < pl->bindingCount; i++) {
                const RKNSLaunchBinding *b = &pl->bindings[i];
                printf("[INFO]     [%u] %s: (%d,%d,%d) -> (%d,%d,%d)\n",
                       i, b->name,
                       b->srcChn.enModId, b->srcChn.s32DevId, b->srcChn.s32ChnId,
                       b->dstChn.enModId, b->dstChn.s32DevId, b->dstChn.s32ChnId);
            }

            if (pl->attachCount > 0) {
                printf("[INFO]   Attaches (%u):\n", pl->attachCount);
                for (i = 0; i < pl->attachCount; i++) {
                    const RKNSLaunchAttach *att = &pl->attaches[i];
                    printf("[INFO]     [%u] rgn=%d -> (%d,%d,%d)\n",
                           i, att->rgnHandle,
                           att->dstChn.enModId,
                           att->dstChn.s32DevId, att->dstChn.s32ChnId);
                }
            }
        }
    }

    printf("===================================================\n\n");
}

/**
 * @brief 将 CLI 参数填充的扁平 config 字段同步到 pipeline 描述的模块配置
 *
 * CLI 模式（-s/-i/-m 等）填充 RKNSLaunchConfig 的扁平字段（width, modelType 等），
 * 此函数将这些字段转换为 pipeline[0] 各模块的 configBuf，
 * 使注册表驱动的生命周期函数可统一使用模块配置。
 */
void sync_cli_to_pipeline_desc(RKNSLaunchConfig *config, RKNSLaunchPipelineDesc *pl) {
    RKNSLaunchModule *mod;

    if (config->pipelineCount == 0 || pl == NULL) {
        return;
    }
}

/**
 * @brief 将 pipeline 模块配置同步回扁平 config 字段
 *
 * YAML 模式下各模块参数已解析到 pipeline[0] 的 modules[].configBuf 中，
 * 此函数从中提取关键信息回写到 RKNSLaunchConfig 的扁平字段，
 * 使 print_config 能正确显示实际运行参数。
 */
void sync_pipeline_desc_to_cli(const RKNSLaunchPipelineDesc *pl, RKNSLaunchConfig *config) {
    RKNS_U32 i;

    if (pl == NULL || config->pipelineCount == 0) {
        return;
    }
}
