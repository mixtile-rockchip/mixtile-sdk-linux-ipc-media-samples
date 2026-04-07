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
RKNS_OVERLAY_PIXEL_FMT_E parse_pixel_format(const char *fmt_str) {
    if (strcmp(fmt_str, "NV12") == 0) {
        return RKNS_OVERLAY_PIXFMT_NV12;
    } else if (strcmp(fmt_str, "RGB888") == 0) {
        return RKNS_OVERLAY_PIXFMT_RGB888;
    } else if (strcmp(fmt_str, "ARGB8888") == 0) {
        return RKNS_OVERLAY_PIXFMT_ARGB8888;
    }
    return RKNS_OVERLAY_PIXFMT_NV12; // 默认
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
    /* RKNS 模块 */
    if (strcmp(name, "INPUT") == 0)    return RKNS_LAUNCH_MOD_INPUT;
    if (strcmp(name, "INFER") == 0)    return RKNS_LAUNCH_MOD_INFER;
    if (strcmp(name, "GRAPHICS") == 0) return RKNS_LAUNCH_MOD_GRAPHICS;
    if (strcmp(name, "OUTPUT") == 0)   return RKNS_LAUNCH_MOD_OUTPUT;
    if (strcmp(name, "SYS") == 0)      return RKNS_LAUNCH_MOD_SYS;
    if (strcmp(name, "MB") == 0)       return RKNS_LAUNCH_MOD_MB;
    if (strcmp(name, "TEXT") == 0)     return RKNS_LAUNCH_MOD_TEXT;
    if (strcmp(name, "IMAGE") == 0)    return RKNS_LAUNCH_MOD_IMAGE;
    if (strcmp(name, "COMPOSE") == 0)  return RKNS_LAUNCH_MOD_COMPOSE;
    if (strcmp(name, "EMBEDDING") == 0) return RKNS_LAUNCH_MOD_EMBEDDING;
    /* Rockit 模块 */
    if (strcmp(name, "VI") == 0)       return RKNS_LAUNCH_MOD_RK_VI;
    if (strcmp(name, "VO") == 0)       return RKNS_LAUNCH_MOD_RK_VO;
    if (strcmp(name, "VPSS") == 0)     return RKNS_LAUNCH_MOD_RK_VPSS;
    if (strcmp(name, "RGN") == 0)      return RKNS_LAUNCH_MOD_RK_RGN;
    if (strcmp(name, "AI") == 0)       return RKNS_LAUNCH_MOD_RK_AI;
    return RKNS_LAUNCH_MOD_BUTT;
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
 * @brief 加载输入配置
 */
static RKNS_S32 load_input_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    const char *type_str;
    const char *path_str;

    // 输入类型
    type_str = yaml_get_string(doc, "input.type", "CUSTOM");
    config->inputType = parse_input_type(type_str);

    // 输入路径
    path_str = yaml_get_string(doc, "input.path", "");
    if (strlen(path_str) > 0) {
        rkos_strncpy(config->inputPath, path_str, MAX_PATH - 1);
    }

    // 分辨率
    config->width = (RKNS_U32)yaml_get_int(doc, "input.width", 640);
    config->height = (RKNS_U32)yaml_get_int(doc, "input.height", 480);

    // 帧率
    config->frameRate = (RKNS_U32)yaml_get_int(doc, "input.frame_rate", 30);

    // Rockit 特定配置
    config->rockitCmd = parse_rockit_cmd(
        yaml_get_string(doc, "input.rockit_cmd", "SEND_VO")
    );

    return RKNS_SUCCESS;
}

/**
 * @brief 加载推理配置
 */
static RKNS_S32 load_inference_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    const char *type_str;
    const char *path_str;

    // 模型类型
    type_str = yaml_get_string(doc, "inference.model_type", "DETECT");
    config->modelType = parse_model_type(type_str);

    // 模型路径
    path_str = yaml_get_string(doc, "inference.model_path", "/oem/models/yolov8.rknn");
    rkos_strncpy(config->modelPath, path_str, MAX_MODEL_PATH - 1);

    // 置信度阈值
    config->confidenceThreshold = (RKNS_FLOAT)yaml_get_float(
        doc, "inference.confidence_threshold", 0.5
    );

    // 队列和缓冲配置
    config->inputQueueCnt = (RKNS_U32)yaml_get_int(doc, "inference.input_queue_count", 3);
    config->outputBufferCnt = (RKNS_U32)yaml_get_int(doc, "inference.output_buffer_count", 3);

    return RKNS_SUCCESS;
}

/**
 * @brief 加载 Overlay 配置
 */
static RKNS_S32 load_overlay_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    const char *fmt_str;
    int rule_count;
    char path[128];

    // 是否启用 Overlay
    config->enableOverlay = (RKNS_BOOL)yaml_get_bool(doc, "overlay.enable", 1);

    if (!config->enableOverlay) {
        return RKNS_SUCCESS;
    }

    // 像素格式
    fmt_str = yaml_get_string(doc, "overlay.pixel_format", "NV12");
    config->pixelFormat = parse_pixel_format(fmt_str);

    // Overlay 属性
    config->overlayAttr.pixelFmt = config->pixelFormat;
    config->overlayAttr.maxBoxPerFrame = (RKNS_U32)yaml_get_int(doc, "overlay.max_box_per_frame", 32);
    config->overlayAttr.lineWidth = (RKNS_U32)yaml_get_int(doc, "overlay.line_width", 2);
    config->overlayAttr.textScale = (RKNS_U32)yaml_get_int(doc, "overlay.text_scale", 1);

    // Overlay 规则 - 基本设置
    config->overlayRule.enableBox = (RKNS_BOOL)yaml_get_bool(doc, "overlay.enable_box", 1);
    config->overlayRule.enableText = (RKNS_BOOL)yaml_get_bool(doc, "overlay.enable_text", 1);
    config->overlayRule.globalScoreThreshold = (RKNS_FLOAT)yaml_get_float(
        doc, "overlay.global_score_threshold", 0.4);

    // Overlay 规则 - 加载类别规则列表
    rule_count = yaml_get_count(doc, "overlay.rules");
    if (rule_count > 0 && rule_count <= RKNS_OVERLAY_MAX_CLS_NUM) {
        int i;
        config->overlayRule.clsNum = (RKNS_U32)rule_count;

        for (i = 0; i < rule_count; i++) {
            RKNS_OVERLAY_RULE_ITEM_S *item = &config->overlayRule.clsRule[i];
            const char *name_str;

            snprintf(path, sizeof(path), "overlay.rules.%d.class_name", i);
            name_str = yaml_get_string(doc, path, "unknown");
            rkos_strncpy(item->clsName, name_str, RKNS_OVERLAY_MAX_NAME_LEN - 1);
            item->clsName[RKNS_OVERLAY_MAX_NAME_LEN - 1] = '\0';

            snprintf(path, sizeof(path), "overlay.rules.%d.enable", i);
            item->enable = (RKNS_BOOL)yaml_get_bool(doc, path, 1);

            snprintf(path, sizeof(path), "overlay.rules.%d.score_threshold", i);
            item->scoreThreshold = (RKNS_FLOAT)yaml_get_float(doc, path, 0.5);

            snprintf(path, sizeof(path), "overlay.rules.%d.color.r", i);
            item->color.r = (RKNS_U8)yaml_get_int(doc, path, 255);

            snprintf(path, sizeof(path), "overlay.rules.%d.color.g", i);
            item->color.g = (RKNS_U8)yaml_get_int(doc, path, 0);

            snprintf(path, sizeof(path), "overlay.rules.%d.color.b", i);
            item->color.b = (RKNS_U8)yaml_get_int(doc, path, 0);

            snprintf(path, sizeof(path), "overlay.rules.%d.color.a", i);
            item->color.a = (RKNS_U8)yaml_get_int(doc, path, 255);
        }
    }

    return RKNS_SUCCESS;
}

/**
 * @brief 加载输出配置
 */
static RKNS_S32 load_output_config(YamlDocument *doc, RKNSLaunchConfig *config) {
    const char *type_str;
    const char *path_str;

    // 输出类型
    type_str = yaml_get_string(doc, "output.type", "APP");
    config->outputType = parse_output_type(type_str);

    // 输出路径
    path_str = yaml_get_string(doc, "output.path", "");
    if (strlen(path_str) > 0) {
        rkos_strncpy(config->outputPath, path_str, MAX_PATH - 1);
    }

    // 观察深度
    config->watchDepth = (RKNS_S32)yaml_get_int(doc, "output.watch_depth", 1);

    // Rockit 特定配置
    config->rockitLayerId = (RKNS_S32)yaml_get_int(doc, "output.vo_layer_id", 0);
    config->rockitChnId = (RKNS_S32)yaml_get_int(doc, "output.vo_channel_id", 0);

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

    // 加载场景配置
    scenario_str = yaml_get_string(&doc, "pipeline.scenario", "detection");
    config->scenario = parse_scenario(scenario_str);

    // 加载各模块配置
    if (load_input_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load input configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    if (load_inference_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load inference configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    if (load_overlay_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load overlay configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    if (load_output_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load output configuration\n");
        ret = RKNS_FAILURE;
        goto cleanup;
    }

    if (load_runtime_config(&doc, config) != RKNS_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load runtime configuration\n");
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

    // 流水线配置
    fprintf(fp, "# 流水线配置\n");
    fprintf(fp, "pipeline:\n");
    fprintf(fp, "  scenario: \"%s\"\n", g_scenarioStr[config->scenario]);
    fprintf(fp, "\n");

    // 输入配置
    fprintf(fp, "# 输入配置\n");
    fprintf(fp, "input:\n");
    fprintf(fp, "  type: \"%s\"\n", g_inputTypeStr[config->inputType]);
    if (strlen(config->inputPath) > 0) {
        fprintf(fp, "  path: \"%s\"\n", config->inputPath);
    }
    fprintf(fp, "  width: %u\n", config->width);
    fprintf(fp, "  height: %u\n", config->height);
    fprintf(fp, "  frame_rate: %u\n", config->frameRate);
    fprintf(fp, "\n");

    // 推理配置
    fprintf(fp, "# 推理配置\n");
    fprintf(fp, "inference:\n");
    fprintf(fp, "  model_type: \"%s\"\n", g_modelTypeStr[config->modelType]);
    fprintf(fp, "  model_path: \"%s\"\n", config->modelPath);
    fprintf(fp, "  confidence_threshold: %.2f\n", config->confidenceThreshold);
    fprintf(fp, "  input_queue_count: %u\n", config->inputQueueCnt);
    fprintf(fp, "  output_buffer_count: %u\n", config->outputBufferCnt);
    fprintf(fp, "\n");

    // Overlay 配置
    fprintf(fp, "# Overlay OSD 配置\n");
    fprintf(fp, "overlay:\n");
    fprintf(fp, "  enable: %s\n", config->enableOverlay ? "true" : "false");
    if (config->enableOverlay) {
        RKNS_U32 i;
        fprintf(fp, "  pixel_format: \"%s\"\n",
                config->pixelFormat == RKNS_OVERLAY_PIXFMT_NV12 ? "NV12" :
                config->pixelFormat == RKNS_OVERLAY_PIXFMT_RGB888 ? "RGB888" : "ARGB8888");
        fprintf(fp, "  max_box_per_frame: %u\n", config->overlayAttr.maxBoxPerFrame);
        fprintf(fp, "  line_width: %u\n", config->overlayAttr.lineWidth);
        fprintf(fp, "  text_scale: %u\n", config->overlayAttr.textScale);
        fprintf(fp, "  enable_box: %s\n", config->overlayRule.enableBox ? "true" : "false");
        fprintf(fp, "  enable_text: %s\n", config->overlayRule.enableText ? "true" : "false");
        fprintf(fp, "  global_score_threshold: %.2f\n", config->overlayRule.globalScoreThreshold);
        if (config->overlayRule.clsNum > 0) {
            fprintf(fp, "  rules:\n");
            for (i = 0; i < config->overlayRule.clsNum; i++) {
                const RKNS_OVERLAY_RULE_ITEM_S *item = &config->overlayRule.clsRule[i];
                fprintf(fp, "    - class_name: \"%s\"\n", item->clsName);
                fprintf(fp, "      enable: %s\n", item->enable ? "true" : "false");
                fprintf(fp, "      score_threshold: %.2f\n", item->scoreThreshold);
                fprintf(fp, "      color:\n");
                fprintf(fp, "        r: %d\n", item->color.r);
                fprintf(fp, "        g: %d\n", item->color.g);
                fprintf(fp, "        b: %d\n", item->color.b);
                fprintf(fp, "        a: %d\n", item->color.a);
            }
        }
    }
    fprintf(fp, "\n");

    // 输出配置
    fprintf(fp, "# 输出配置\n");
    fprintf(fp, "output:\n");
    fprintf(fp, "  type: \"%s\"\n", g_outputTypeStr[config->outputType]);
    if (strlen(config->outputPath) > 0) {
        fprintf(fp, "  path: \"%s\"\n", config->outputPath);
    }
    fprintf(fp, "  watch_depth: %d\n", config->watchDepth);
    fprintf(fp, "\n");

    // 运行时配置
    fprintf(fp, "# 运行时配置\n");
    fprintf(fp, "runtime:\n");
    fprintf(fp, "  timeout: %d\n", config->timeout);
    fprintf(fp, "  profiling: %s\n", config->enableProfiling ? "true" : "false");
    fprintf(fp, "  verbose: %s\n", config->verbose ? "true" : "false");
    fprintf(fp, "\n");

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

    // 场景配置
    config->scenario = RKNS_LAUNCH_SCENARIO_DETECTION;

    // 输入配置
    config->inputType = RKNS_INPUT_SOURCE_CUSTOM;
    config->inputPath[0] = '\0';
    config->width = 640;
    config->height = 480;
    config->frameRate = 30;

    // 推理配置
    config->modelType = RKNS_MODEL_TYPE_DETECT;
    // strncpy(config->modelPath, "/oem/models/yolov8.rknn", MAX_MODEL_PATH - 1);
    config->modelPath[0] = '\0';    // 默认空路径，需用户指定

    config->modelPath[MAX_MODEL_PATH - 1] = '\0';
    config->confidenceThreshold = 0.5f;
    config->inputQueueCnt = 3;
    config->outputBufferCnt = 3;

    // Overlay 配置
    config->enableOverlay = RKNS_TRUE;
    config->pixelFormat = RKNS_OVERLAY_PIXFMT_NV12;

    // Overlay 属性默认值
    config->overlayAttr.width = config->width;
    config->overlayAttr.height = config->height;
    config->overlayAttr.stride = config->width;
    config->overlayAttr.pixelFmt = RKNS_OVERLAY_PIXFMT_NV12;
    config->overlayAttr.bgColor.a = 0xFF;
    config->overlayAttr.bgColor.r = 0;
    config->overlayAttr.bgColor.g = 0;
    config->overlayAttr.bgColor.b = 0;
    config->overlayAttr.maxBoxPerFrame = 32;
    config->overlayAttr.lineWidth = 2;
    config->overlayAttr.textScale = 1;

    // Overlay 规则默认值
    config->overlayRule.enableBox = RKNS_TRUE;
    config->overlayRule.enableText = RKNS_TRUE;
    config->overlayRule.globalScoreThreshold = 0.4f;
    config->overlayRule.clsNum = 3;

    config->overlayRule.clsRule[0].enable = RKNS_TRUE;
    config->overlayRule.clsRule[0].scoreThreshold = 0.3f;
    config->overlayRule.clsRule[0].color.a = 0xFF;
    config->overlayRule.clsRule[0].color.r = 0xFF;
    config->overlayRule.clsRule[0].color.g = 0x00;
    config->overlayRule.clsRule[0].color.b = 0x00;
    rkos_strncpy(config->overlayRule.clsRule[0].clsName, "face", RKNS_OVERLAY_MAX_NAME_LEN - 1);

    config->overlayRule.clsRule[1].enable = RKNS_TRUE;
    config->overlayRule.clsRule[1].scoreThreshold = 0.5f;
    config->overlayRule.clsRule[1].color.a = 0xFF;
    config->overlayRule.clsRule[1].color.r = 0x00;
    config->overlayRule.clsRule[1].color.g = 0xFF;
    config->overlayRule.clsRule[1].color.b = 0x00;
    rkos_strncpy(config->overlayRule.clsRule[1].clsName, "body", RKNS_OVERLAY_MAX_NAME_LEN - 1);

    config->overlayRule.clsRule[2].enable = RKNS_TRUE;
    config->overlayRule.clsRule[2].scoreThreshold = 0.6f;
    config->overlayRule.clsRule[2].color.a = 0xFF;
    config->overlayRule.clsRule[2].color.r = 0x00;
    config->overlayRule.clsRule[2].color.g = 0x80;
    config->overlayRule.clsRule[2].color.b = 0xFF;
    rkos_strncpy(config->overlayRule.clsRule[2].clsName, "car", RKNS_OVERLAY_MAX_NAME_LEN - 1);

    // 输出配置
    config->outputType = RKNS_OUTPUT_TYPE_APP;
    config->outputPath[0] = '\0';
    config->watchDepth = 1;

    // Rockit 特定配置
    config->rockitCmd = RKNS_ROCKIT_CMD_SEND_VO;
    config->rockitLayerId = 0;
    config->rockitChnId = 0;

    // 运行时配置
    config->timeout = 0;
    config->enableProfiling = RKNS_FALSE;
    config->verbose = RKNS_TRUE;

    // 默认 pipeline：rknnstream, INPUT -> INFER -> GRAPHICS -> OUTPUT
    config->pipelineCount = 1;
    {
        RKNSLaunchPipelineDesc *pl = &config->pipelines[0];

        rkos_strncpy(pl->name, "rknnstream_pipeline", MAX_PIPELINE_NAME - 1);
        pl->type = RKNS_LAUNCH_PIPELINE_TYPE_RKNNSTREAM;
        pl->enabled = RKNS_TRUE;

        pl->moduleCount = 4;
        rkos_strncpy(pl->modules[0].name, "INPUT", MAX_MODULE_NAME - 1);
        pl->modules[0].modId = RKNS_LAUNCH_MOD_INPUT;
        pl->modules[0].devId = 0;
        pl->modules[0].chnId = 0;
        rkos_strncpy(pl->modules[1].name, "INFER", MAX_MODULE_NAME - 1);
        pl->modules[1].modId = RKNS_LAUNCH_MOD_INFER;
        pl->modules[1].devId = 0;
        pl->modules[1].chnId = 0;
        rkos_strncpy(pl->modules[2].name, "GRAPHICS", MAX_MODULE_NAME - 1);
        pl->modules[2].modId = RKNS_LAUNCH_MOD_GRAPHICS;
        pl->modules[2].devId = 0;
        pl->modules[2].chnId = 0;
        rkos_strncpy(pl->modules[3].name, "OUTPUT", MAX_MODULE_NAME - 1);
        pl->modules[3].modId = RKNS_LAUNCH_MOD_OUTPUT;
        pl->modules[3].devId = 0;
        pl->modules[3].chnId = 0;

        pl->bindingCount = 3;
        rkos_strncpy(pl->bindings[0].name, "input_to_infer", MAX_BINDING_NAME - 1);
        pl->bindings[0].srcChn.enModId = (RKNS_MOD_ID_E)RKNS_LAUNCH_MOD_INPUT;
        pl->bindings[0].srcChn.s32DevId = 0;
        pl->bindings[0].srcChn.s32ChnId = 0;
        pl->bindings[0].dstChn.enModId = (RKNS_MOD_ID_E)RKNS_LAUNCH_MOD_INFER;
        pl->bindings[0].dstChn.s32DevId = 0;
        pl->bindings[0].dstChn.s32ChnId = 0;

        rkos_strncpy(pl->bindings[1].name, "infer_to_graphics", MAX_BINDING_NAME - 1);
        pl->bindings[1].srcChn.enModId = (RKNS_MOD_ID_E)RKNS_LAUNCH_MOD_INFER;
        pl->bindings[1].srcChn.s32DevId = 0;
        pl->bindings[1].srcChn.s32ChnId = 0;
        pl->bindings[1].dstChn.enModId = (RKNS_MOD_ID_E)RKNS_LAUNCH_MOD_GRAPHICS;
        pl->bindings[1].dstChn.s32DevId = 0;
        pl->bindings[1].dstChn.s32ChnId = 0;

        rkos_strncpy(pl->bindings[2].name, "graphics_to_output", MAX_BINDING_NAME - 1);
        pl->bindings[2].srcChn.enModId = (RKNS_MOD_ID_E)RKNS_LAUNCH_MOD_GRAPHICS;
        pl->bindings[2].srcChn.s32DevId = 0;
        pl->bindings[2].srcChn.s32ChnId = 0;
        pl->bindings[2].dstChn.enModId = (RKNS_MOD_ID_E)RKNS_LAUNCH_MOD_OUTPUT;
        pl->bindings[2].dstChn.s32DevId = 0;
        pl->bindings[2].dstChn.s32ChnId = 0;

        pl->attachCount = 0;
    }
}

/**
 * @brief 打印配置信息
 */
void print_config(const RKNSLaunchConfig *config) {
    if (!config) {
        return;
    }

    printf("\n========== RKNNStream Launch Configuration ==========\n");

    // 场景配置
    printf("[INFO] Scenario: %s\n", g_scenarioStr[config->scenario]);

    // 输入配置
    printf("[INFO] Input Type: %s\n", g_inputTypeStr[config->inputType]);
    if (strlen(config->inputPath) > 0) {
        printf("[INFO] Input Path: %s\n", config->inputPath);
    }
    printf("[INFO] Resolution: %ux%u\n", config->width, config->height);
    printf("[INFO] Frame Rate: %u\n", config->frameRate);

    // 推理配置
    printf("[INFO] Model Type: %s\n", g_modelTypeStr[config->modelType]);
    printf("[INFO] Model Path: %s\n", config->modelPath);
    printf("[INFO] Confidence Threshold: %.2f\n", config->confidenceThreshold);
    printf("[INFO] Input Queue Count: %u\n", config->inputQueueCnt);
    printf("[INFO] Output Buffer Count: %u\n", config->outputBufferCnt);

    // Overlay 配置
    printf("[INFO] Overlay Enabled: %s\n",
           config->enableOverlay ? "true" : "false");
    if (config->enableOverlay) {
        RKNS_U32 i;
        printf("[INFO]   Pixel Format: %s\n",
               config->pixelFormat == RKNS_OVERLAY_PIXFMT_NV12 ? "NV12" :
               config->pixelFormat == RKNS_OVERLAY_PIXFMT_RGB888 ? "RGB888" : "ARGB8888");
        printf("[INFO]   MaxBoxPerFrame: %u\n", config->overlayAttr.maxBoxPerFrame);
        printf("[INFO]   LineWidth: %u, TextScale: %u\n",
               config->overlayAttr.lineWidth, config->overlayAttr.textScale);
        printf("[INFO]   Rule: enableBox=%s, enableText=%s\n",
               config->overlayRule.enableBox ? "true" : "false",
               config->overlayRule.enableText ? "true" : "false");
        printf("[INFO]   Global Score Threshold: %.2f\n",
               config->overlayRule.globalScoreThreshold);
        printf("[INFO]   Class Rules (%u):\n", config->overlayRule.clsNum);
        for (i = 0; i < config->overlayRule.clsNum; i++) {
            const RKNS_OVERLAY_RULE_ITEM_S *item = &config->overlayRule.clsRule[i];
            printf("[INFO]     [%u] name=%s, enable=%s, score=%.2f, color=(%d,%d,%d,%d)\n",
                   i, item->clsName,
                   item->enable ? "true" : "false",
                   item->scoreThreshold,
                   item->color.a, item->color.r, item->color.g, item->color.b);
        }
    }

    // 输出配置
    printf("[INFO] Output Type: %s\n", g_outputTypeStr[config->outputType]);
    if (strlen(config->outputPath) > 0) {
        printf("[INFO] Output Path: %s\n", config->outputPath);
    }
    printf("[INFO] Watch Depth: %d\n", config->watchDepth);

    // 运行时配置
    printf("[INFO] Timeout: %d seconds\n", config->timeout);
    printf("[INFO] Profiling: %s\n",
           config->enableProfiling ? "enabled" : "disabled");
    printf("[INFO] Verbose: %s\n",
           config->verbose ? "enabled" : "disabled");

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

    /* INPUT 模块 */
    mod = &pl->modules[0];
    {
        RKNSInputParam *param = MODULE_CONFIG(mod, RKNSInputParam);
        rkos_memset(param, 0, sizeof(RKNSInputParam));
        param->sourceType = config->inputType;
        param->watcherDepth = 1;
        mod->configParsed = RKNS_TRUE;
    }

    /* INFER 模块 */
    mod = &pl->modules[1];
    {
        RKNS_INFER_ATTR_S *attr = MODULE_CONFIG(mod, RKNS_INFER_ATTR_S);
        rkos_memset(attr, 0, sizeof(RKNS_INFER_ATTR_S));
        attr->enModelType = config->modelType;
        attr->u32InputQueueCnt = config->inputQueueCnt;
        attr->u32BufferCnt = config->outputBufferCnt;
        mod->configParsed = RKNS_TRUE;
    }

    /* GRAPHICS 模块 */
    mod = &pl->modules[2];
    {
        RKNSGraphicsParam *param = MODULE_CONFIG(mod, RKNSGraphicsParam);
        RKNS_OVERLAY_RULE_S *rule = MODULE_SECONDARY(mod, RKNS_OVERLAY_RULE_S);
        rkos_memset(param, 0, sizeof(RKNSGraphicsParam));
        rkos_memset(rule, 0, sizeof(RKNS_OVERLAY_RULE_S));

        if (config->enableOverlay) {
            param->type = RKNS_GRAPHICS_TYPE_OVERLAY;
            param->attr = config->overlayAttr;
            param->attr.stride = config->width;
            *rule = config->overlayRule;
            mod->hasSecondaryConfig = RKNS_TRUE;
        }
        mod->configParsed = RKNS_TRUE;
    }

    /* OUTPUT 模块 */
    mod = &pl->modules[3];
    {
        RKNSOutputParam *param = MODULE_CONFIG(mod, RKNSOutputParam);
        rkos_memset(param, 0, sizeof(RKNSOutputParam));
        param->type = config->outputType;
        param->watchDepth = config->watchDepth;
        mod->configParsed = RKNS_TRUE;
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

    for (i = 0; i < pl->moduleCount; i++) {
        const RKNSLaunchModule *mod = &pl->modules[i];

        if (mod->modId == RKNS_LAUNCH_MOD_INPUT && mod->configParsed) {
            const RKNSInputParam *param = MODULE_CONFIG(mod, RKNSInputParam);
            config->inputType = param->sourceType;
        } else if (mod->modId == RKNS_LAUNCH_MOD_INFER && mod->configParsed) {
            const RKNS_INFER_ATTR_S *attr = MODULE_CONFIG(mod, RKNS_INFER_ATTR_S);
            config->modelType = attr->enModelType;
            config->inputQueueCnt = attr->u32InputQueueCnt;
            config->outputBufferCnt = attr->u32BufferCnt;
            if (attr->stModelAttr.pDataPath) {
                rkos_strncpy(config->modelPath, attr->stModelAttr.pDataPath,
                             MAX_MODEL_PATH - 1);
                config->modelPath[MAX_MODEL_PATH - 1] = '\0';
            }
        } else if (mod->modId == RKNS_LAUNCH_MOD_GRAPHICS && mod->configParsed) {
            RKNSGraphicsParam *param = MODULE_CONFIG(mod, RKNSGraphicsParam);
            config->enableOverlay = (param->type == RKNS_GRAPHICS_TYPE_OVERLAY)
                                    ? RKNS_TRUE : RKNS_FALSE;
        } else if (mod->modId == RKNS_LAUNCH_MOD_OUTPUT && mod->configParsed) {
            RKNSOutputParam *param = MODULE_CONFIG(mod, RKNSOutputParam);
            config->outputType = param->type;
            config->watchDepth = param->watchDepth;
        }
    }
}
