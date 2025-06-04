#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <signal.h>
#include "cJSON.h"

#include "rk_debug.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_venc.h"
#include "rk_comm_rc.h"

#undef SMART_V3

#define PORT 12345
#define BUFFER_SIZE 8192

#define OPEN_VENC_SET \
    FILE* fp = fopen("/tmp/venc_set.c", access("/tmp/venc_set.c", F_OK) == 0 ? "r+" : "a+"); \
    char *file_data = NULL; \
    long file_size = 0; \
    do { \
        if (fp) { \
            fseek(fp, 0, SEEK_END); \
            file_size = ftell(fp); \
            fseek(fp, 0, SEEK_SET); \
            file_data = (char *)malloc(file_size); \
            if (!file_data) { \
                printf("malloc file data failed! clear file data!\n"); \
                fclose(fp); \
                fp = fopen("/tmp/venc_set.c", "w+"); \
            } else { \
                fread(file_data, 1, file_size, fp); \
                fseek(fp, 0, SEEK_SET); \
            } \
        } \
    } while (0)

#define CLOSE_VENC_SET \
    do { \
        if (fp) { \
            if (file_data) { \
                fwrite(file_data, 1, file_size, fp); \
                free(file_data); \
            } \
            fclose(fp); \
        } \
    } while (0)

int server_fd = -1;

void handle_signal(int sig);
void check_and_set_qbias_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_qp_params(int venc_id, const char* type, const cJSON* venc);
void parse_and_print_specific_fields(const cJSON* json);
char* format_json_with_cjson(const char* input);
void check_and_set_qp_dlt_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_aq_rnge_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_scene_mode(int venc_id, const char* type, const cJSON* venc);
void check_and_set_flt_str_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_md_swth_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_deblur_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_atr_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_atl_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_atf_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_lambda_idx_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_static_frame_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_lgt_chg_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_dblk_mode_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_dblk_en_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_smt1_en_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_smt3_en_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_svc3_qp_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_intra_smoothing_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_tmvp_en_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_gop_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_trans_scaling_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_rc_mode_bitrate(int venc_id, const char* type, const cJSON* venc);
void check_and_set_cu_dqp_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_debreath_strength_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_color_range_params(int venc_id, const char* type, const cJSON* venc);
void check_and_set_inst_br_lvl_params(int venc_id, const char* type, const cJSON* venc);

void check_and_set_sao_params(int venc_id, const char* type, const cJSON* venc);

void handle_signal(int sig) {
    if (server_fd != -1) {
        close(server_fd);
    }
    exit(0);
}

char* get_venc_info() {
    FILE* fp;
    char* buffer = NULL;
    size_t buffer_size = 0;
    size_t content_size = 0;
    char chunk[1024];

    fp = popen("cat /proc/vcodec/enc/venc_info", "r");
    if (fp == NULL) {
        return NULL;
    }

    while (fgets(chunk, sizeof(chunk), fp) != NULL) {
        size_t chunk_len = strlen(chunk);
        char* new_buffer = realloc(buffer, buffer_size + chunk_len + 1);

        if (new_buffer == NULL) {
            free(buffer);
            pclose(fp);
            return NULL;
        }

        buffer = new_buffer;
        memcpy(buffer + content_size, chunk, chunk_len);
        content_size += chunk_len;
        buffer_size = content_size;
        buffer[content_size] = '\0';
    }

    int status = pclose(fp);
    if (status != 0) {
        free(buffer);
        return NULL;
    }

    if (buffer == NULL || content_size == 0) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

char* get_venc_set() {
    FILE* fp;
    char* buffer = NULL;
    size_t buffer_size = 0;
    size_t content_size = 0;
    char chunk[1024];

    fp = popen("cat /tmp/venc_set.c", "r");
    if (fp == NULL) {
        return NULL;
    }

    while (fgets(chunk, sizeof(chunk), fp) != NULL) {
        size_t chunk_len = strlen(chunk);
        char* new_buffer = realloc(buffer, buffer_size + chunk_len + 1);

        if (new_buffer == NULL) {
            free(buffer);
            pclose(fp);
            return NULL;
        }

        buffer = new_buffer;
        memcpy(buffer + content_size, chunk, chunk_len);
        content_size += chunk_len;
        buffer_size = content_size;
        buffer[content_size] = '\0';
    }

    int status = pclose(fp);
    if (status != 0) {
        free(buffer);
        return NULL;
    }

    if (buffer == NULL || content_size == 0) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

void parse_and_print_specific_fields(const cJSON* json) {
    if (!json || !cJSON_IsObject(json)) {
        return;
    }

    cJSON* venc_obj;
    cJSON_ArrayForEach(venc_obj, json) {
        if (cJSON_IsObject(venc_obj)) {
            char* venc_key = venc_obj->string;
            if (venc_key && strncmp(venc_key, "venc", 4) == 0) {
                int venc_id = atoi(venc_key + 4);  // 跳过"venc"前缀

                cJSON* type = cJSON_GetObjectItem(venc_obj, "Type");
                const char* type_str = NULL;
                if (type && cJSON_IsString(type)) {
                    type_str = type->valuestring;
                }

                check_and_set_smt1_en_params(venc_id, type_str, venc_obj);

#ifdef SMART_V3
                check_and_set_smt3_en_params(venc_id, type_str, venc_obj);

                check_and_set_svc3_qp_params(venc_id, type_str, venc_obj);
#endif

                check_and_set_scene_mode(venc_id, type_str, venc_obj);

                check_and_set_md_swth_params(venc_id, type_str, venc_obj);

                check_and_set_qbias_params(venc_id, type_str, venc_obj);

                check_and_set_qp_params(venc_id, type_str, venc_obj);

                check_and_set_qp_dlt_params(venc_id, type_str, venc_obj);

                check_and_set_aq_rnge_params(venc_id, type_str, venc_obj);

                check_and_set_flt_str_params(venc_id, type_str, venc_obj);

                check_and_set_deblur_params(venc_id, type_str, venc_obj);

                check_and_set_atr_params(venc_id, type_str, venc_obj);

                check_and_set_atl_params(venc_id, type_str, venc_obj);

                check_and_set_atf_params(venc_id, type_str, venc_obj);

                check_and_set_lambda_idx_params(venc_id, type_str, venc_obj);

                check_and_set_static_frame_params(venc_id, type_str, venc_obj);

                check_and_set_lgt_chg_params(venc_id, type_str, venc_obj);

                check_and_set_dblk_mode_params(venc_id, type_str, venc_obj);

                check_and_set_dblk_en_params(venc_id, type_str, venc_obj);

                check_and_set_sao_params(venc_id, type_str, venc_obj);

                check_and_set_intra_smoothing_params(venc_id, type_str, venc_obj);

                check_and_set_tmvp_en_params(venc_id, type_str, venc_obj);

                check_and_set_gop_params(venc_id, type_str, venc_obj);

                check_and_set_trans_scaling_params(venc_id, type_str, venc_obj);

                check_and_set_rc_mode_bitrate(venc_id, type_str, venc_obj);

                check_and_set_cu_dqp_params(venc_id, type_str, venc_obj);

                check_and_set_debreath_strength_params(venc_id, type_str, venc_obj);

                check_and_set_color_range_params(venc_id, type_str, venc_obj);

#if defined(RV1103B) || defined(RV1126B)
                check_and_set_inst_br_lvl_params(venc_id, type_str, venc_obj);
#endif

                cJSON* child;
                cJSON_ArrayForEach(child, venc_obj) {
                    if (cJSON_IsObject(child)) {
                        parse_and_print_specific_fields(child);
                    }
                }
            }
        }
    }
}

char* format_json_with_cjson(const char* input) {
    cJSON* json = cJSON_Parse(input);
    if (!json) {
        return NULL;
    }

    parse_and_print_specific_fields(json);

    char* formatted = cJSON_Print(json);

    cJSON_Delete(json);

    return formatted;
}

void check_and_set_qbias_params(int venc_id, const char* type, const cJSON* venc) {
    int qbias_en_changed = 0;
    int qbias_i_changed = 0;
    int qbias_p_changed = 0;
    int qbias_arr_changed = 0;

    cJSON* qbias_en = cJSON_GetObjectItem(venc, "qbias_en");
    if (qbias_en) {
        if (cJSON_IsNumber(qbias_en)) {
            qbias_en_changed = 1;
        }
    }

    cJSON* qbias_i = cJSON_GetObjectItem(venc, "qbias_i");
    if (qbias_i) {
        if (cJSON_IsNumber(qbias_i)) {
            qbias_i_changed = 1;
        }
    }

    cJSON* qbias_p = cJSON_GetObjectItem(venc, "qbias_p");
    if (qbias_p) {
        if (cJSON_IsNumber(qbias_p)) {
            qbias_p_changed = 1;
        }
    }

    cJSON* qbias_arr = cJSON_GetObjectItem(venc, "qbias_arr");
    if (qbias_arr) {
        if (cJSON_IsArray(qbias_arr)) {
            qbias_arr_changed = 1;
        }
    }

    if (qbias_en_changed || qbias_i_changed || qbias_p_changed || qbias_arr_changed) {
        if ((qbias_en_changed || qbias_i_changed || qbias_p_changed) && !qbias_arr_changed) {
            if (type && strcasecmp(type, "h265") == 0) {
                VENC_H265_QBIAS_S stQbias;
                RK_MPI_VENC_GetH265Qbias(venc_id, &stQbias);
                if (qbias_en_changed) {
                    stQbias.bEnable = qbias_en->valueint;
                }
                if (qbias_i_changed) {
                    stQbias.u32QbiasI = qbias_i->valueint;
                }
                if (qbias_p_changed) {
                    stQbias.u32QbiasP = qbias_p->valueint;
                }
                RK_MPI_VENC_SetH265Qbias(venc_id, &stQbias);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_H265_QBIAS_S stQbias;\n"
                             "RK_MPI_VENC_GetH265Qbias(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);

                    if (qbias_en_changed) {
                        snprintf(str, sizeof(str), "stQbias.bEnable = %d;\n", qbias_en->valueint);
                        fputs(str, fp);
                    }
                    if (qbias_i_changed) {
                        snprintf(str, sizeof(str), "stQbias.u32QbiasI = %d;\n", qbias_i->valueint);
                        fputs(str, fp);
                    }
                    if (qbias_p_changed) {
                        snprintf(str, sizeof(str), "stQbias.u32QbiasP = %d;\n", qbias_p->valueint);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetH265Qbias(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h264") == 0) {
                VENC_H264_QBIAS_S stQbias;
                RK_MPI_VENC_GetH264Qbias(venc_id, &stQbias);
                if (qbias_en_changed) {
                    stQbias.bEnable = qbias_en->valueint;
                }
                if (qbias_i_changed) {
                    stQbias.u32QbiasI = qbias_i->valueint;
                }
                if (qbias_p_changed) {
                    stQbias.u32QbiasP = qbias_p->valueint;
                }
                RK_MPI_VENC_SetH264Qbias(venc_id, &stQbias);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_H264_QBIAS_S stQbias;\n"
                             "RK_MPI_VENC_GetH264Qbias(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);

                    if (qbias_en_changed) {
                        snprintf(str, sizeof(str), "stQbias.bEnable = %d;\n", qbias_en->valueint);
                        fputs(str, fp);
                    }
                    if (qbias_i_changed) {
                        snprintf(str, sizeof(str), "stQbias.u32QbiasI = %d;\n", qbias_i->valueint);
                        fputs(str, fp);
                    }
                    if (qbias_p_changed) {
                        snprintf(str, sizeof(str), "stQbias.u32QbiasP = %d;\n", qbias_p->valueint);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetH264Qbias(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            }
        }
        else if (qbias_en_changed && qbias_arr_changed) {
#ifndef RV1106_RV1103
            if (type && strcasecmp(type, "h265") == 0) {
                VENC_H265_QBIAS2_S stQbias;
                RK_MPI_VENC_GetH265Qbias2(venc_id, &stQbias);
                if (qbias_en_changed) {
                    stQbias.bEnable = qbias_en->valueint;
                }
                if (qbias_arr_changed) {
                    int size = cJSON_GetArraySize(qbias_arr);
                    for (int i = 0; i < size; i++) {
                        cJSON* item = cJSON_GetArrayItem(qbias_arr, i);
                        if (cJSON_IsNumber(item)) {
                            stQbias.u32Qbias2[i] = item->valueint;
                        }
                    }
                }
                RK_MPI_VENC_SetH265Qbias2(venc_id, &stQbias);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_H265_QBIAS2_S stQbias;\n"
                             "RK_MPI_VENC_GetH265Qbias2(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);

                    if (qbias_en_changed) {
                        snprintf(str, sizeof(str), "stQbias.bEnable = %d;\n", qbias_en->valueint);
                        fputs(str, fp);
                    }

                    if (qbias_arr_changed) {
                        int size = cJSON_GetArraySize(qbias_arr);
                        for (int i = 0; i < size; i++) {
                            cJSON* item = cJSON_GetArrayItem(qbias_arr, i);
                            if (cJSON_IsNumber(item)) {
                                snprintf(str, sizeof(str), "stQbias.u32Qbias2[%d] = %d;\n", i, item->valueint);
                                fputs(str, fp);
                            }
                        }
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetH265Qbias2(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h264") == 0) {
                VENC_H264_QBIAS2_S stQbias;
                RK_MPI_VENC_GetH264Qbias2(venc_id, &stQbias);
                if (qbias_en_changed) {
                    stQbias.bEnable = qbias_en->valueint;
                }
                if (qbias_arr_changed) {
                    int size = cJSON_GetArraySize(qbias_arr);
                    for (int i = 0; i < size; i++) {
                        cJSON* item = cJSON_GetArrayItem(qbias_arr, i);
                        if (cJSON_IsNumber(item)) {
                            stQbias.u32Qbias2[i] = item->valueint;
                        }
                    }
                }
                RK_MPI_VENC_SetH264Qbias2(venc_id, &stQbias);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_H264_QBIAS2_S stQbias;\n"
                             "RK_MPI_VENC_GetH264Qbias2(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);

                    if (qbias_en_changed) {
                        snprintf(str, sizeof(str), "stQbias.bEnable = %d;\n", qbias_en->valueint);
                        fputs(str, fp);
                    }

                    if (qbias_arr_changed) {
                        int size = cJSON_GetArraySize(qbias_arr);
                        for (int i = 0; i < size; i++) {
                            cJSON* item = cJSON_GetArrayItem(qbias_arr, i);
                            if (cJSON_IsNumber(item)) {
                                snprintf(str, sizeof(str), "stQbias.u32Qbias2[%d] = %d;\n", i, item->valueint);
                                fputs(str, fp);
                            }
                        }
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetH264Qbias2(%d, &stQbias);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            }
#endif
        }
    }
}

void check_and_set_qp_params(int venc_id, const char* type, const cJSON* venc) {
    int max_qp_changed = 0;
    int min_qp_changed = 0;
    int max_iqp_changed = 0;
    int min_iqp_changed = 0;
    int frm_max_qp_changed = 0;
    int frm_min_qp_changed = 0;
    int frm_max_iqp_changed = 0;
    int frm_min_iqp_changed = 0;
    int max_reenc_times_changed = 0;
    int ip_qp_delta_changed = 0;
    int md_switch_qp_changed = 0;
    int start_qp_changed = 0;

    int max_qp = -1;
    int min_qp = -1;
    int max_iqp = -1;
    int min_iqp = -1;
    int frm_max_qp = -1;
    int frm_min_qp = -1;
    int frm_max_iqp = -1;
    int frm_min_iqp = -1;
    int max_reenc_times = -1;
    int ip_qp_delta = -1;
    int md_switch_qp = -1;
    int start_qp = -1;

    cJSON* json_max_qp = cJSON_GetObjectItem(venc, "MaxQp");
    if (json_max_qp && cJSON_IsNumber(json_max_qp)) {
        max_qp = json_max_qp->valueint;
        max_qp_changed = 1;
    }

    cJSON* json_min_qp = cJSON_GetObjectItem(venc, "MinQp");
    if (json_min_qp && cJSON_IsNumber(json_min_qp)) {
        min_qp = json_min_qp->valueint;
        min_qp_changed = 1;
    }

    cJSON* json_max_iqp = cJSON_GetObjectItem(venc, "MaxIQp");
    if (json_max_iqp && cJSON_IsNumber(json_max_iqp)) {
        max_iqp = json_max_iqp->valueint;
        max_iqp_changed = 1;
    }

    cJSON* json_min_iqp = cJSON_GetObjectItem(venc, "MinIQp");
    if (json_min_iqp && cJSON_IsNumber(json_min_iqp)) {
        min_iqp = json_min_iqp->valueint;
        min_iqp_changed = 1;
    }

    cJSON* json_frm_max_qp = cJSON_GetObjectItem(venc, "FrmMaxQp");
    if (json_frm_max_qp && cJSON_IsNumber(json_frm_max_qp)) {
        frm_max_qp = json_frm_max_qp->valueint;
        frm_max_qp_changed = 1;
    }

    cJSON* json_frm_min_qp = cJSON_GetObjectItem(venc, "FrmMinQp");
    if (json_frm_min_qp && cJSON_IsNumber(json_frm_min_qp)) {
        frm_min_qp = json_frm_min_qp->valueint;
        frm_min_qp_changed = 1;
    }

    cJSON* json_frm_max_iqp = cJSON_GetObjectItem(venc, "FrmMaxIQp");
    if (json_frm_max_iqp && cJSON_IsNumber(json_frm_max_iqp)) {
        frm_max_iqp = json_frm_max_iqp->valueint;
        frm_max_iqp_changed = 1;
    }

    cJSON* json_frm_min_iqp = cJSON_GetObjectItem(venc, "FrmMinIQp");
    if (json_frm_min_iqp && cJSON_IsNumber(json_frm_min_iqp)) {
        frm_min_iqp = json_frm_min_iqp->valueint;
        frm_min_iqp_changed = 1;
    }

    cJSON* json_max_reenc_times = cJSON_GetObjectItem(venc, "MaxReEncTimes");
    if (json_max_reenc_times && cJSON_IsNumber(json_max_reenc_times)) {
        max_reenc_times = json_max_reenc_times->valueint;
        max_reenc_times_changed = 1;
    }

    cJSON* json_ip_qp_delta = cJSON_GetObjectItem(venc, "IpQpDelta");
    if (json_ip_qp_delta && cJSON_IsNumber(json_ip_qp_delta)) {
        ip_qp_delta = json_ip_qp_delta->valueint;
        ip_qp_delta_changed = 1;
    }

    cJSON* json_md_switch_qp = cJSON_GetObjectItem(venc, "md_switch_qp");
    if (json_md_switch_qp && cJSON_IsNumber(json_md_switch_qp)) {
        md_switch_qp = json_md_switch_qp->valueint;
        md_switch_qp_changed = 1;
    }

    cJSON* json_start_qp = cJSON_GetObjectItem(venc, "StartQp");
    if (json_start_qp && cJSON_IsNumber(json_start_qp)) {
        start_qp = json_start_qp->valueint;
        start_qp_changed = 1;
    }

    if (max_qp_changed || min_qp_changed || max_iqp_changed || min_iqp_changed || 
        frm_max_qp_changed || frm_min_qp_changed || frm_max_iqp_changed || frm_min_iqp_changed || 
        max_reenc_times_changed || ip_qp_delta_changed || md_switch_qp_changed || start_qp_changed) {
        if (type && strcasecmp(type, "h265") == 0) {
            VENC_RC_PARAM_S stRcParam;
            RK_MPI_VENC_GetRcParam(venc_id, &stRcParam);
            if (start_qp_changed) {
                stRcParam.s32FirstFrameStartQp = start_qp;
            }
            if (max_qp_changed) {
                stRcParam.stParamH265.u32MaxQp = max_qp;
            }
            if (min_qp_changed) {
                stRcParam.stParamH265.u32MinQp = min_qp;
            }
            if (max_iqp_changed) {
                stRcParam.stParamH265.u32MaxIQp = max_iqp;
            }
            if (min_iqp_changed) {
                stRcParam.stParamH265.u32MinIQp = min_iqp;
            }
            if (ip_qp_delta_changed) {
                stRcParam.stParamH265.s32DeltIpQp = ip_qp_delta;
            }
            if (max_reenc_times_changed) {
                stRcParam.stParamH265.s32MaxReEncodeTimes = max_reenc_times;
            }
            if (frm_max_qp_changed) {
                stRcParam.stParamH265.u32FrmMaxQp = frm_max_qp;
            }
            if (frm_min_qp_changed) {
                stRcParam.stParamH265.u32FrmMinQp = frm_min_qp;
            }
            if (frm_max_iqp_changed) {
                stRcParam.stParamH265.u32FrmMaxIQp = frm_max_iqp;
            }
            if (frm_min_iqp_changed) {
                stRcParam.stParamH265.u32FrmMinIQp = frm_min_iqp;
            }
            if (md_switch_qp_changed) {
                stRcParam.stParamH265.u32MotionStaticSwitchFrmQp = md_switch_qp;
            }
            RK_MPI_VENC_SetRcParam(venc_id, &stRcParam);

            OPEN_VENC_SET;
            if (fp) {
                char str[1024];
                fputs("{\n", fp);
                snprintf(str, sizeof(str), 
                         "VENC_RC_PARAM_S stRcParam;\n"
                         "RK_MPI_VENC_GetRcParam(%d, &stRcParam);\n", venc_id);
                fputs(str, fp);

                if (start_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.s32FirstFrameStartQp = %d;\n", start_qp);
                    fputs(str, fp);
                }
                if (max_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32MaxQp = %d;\n", max_qp);
                    fputs(str, fp);
                }
                if (min_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32MinQp = %d;\n", min_qp);
                    fputs(str, fp);
                }
                if (max_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32MaxIQp = %d;\n", max_iqp);
                    fputs(str, fp);
                }
                if (min_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32MinIQp = %d;\n", min_iqp);
                    fputs(str, fp);
                }
                if (ip_qp_delta_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.s32DeltIpQp = %d;\n", ip_qp_delta);
                    fputs(str, fp);
                }
                if (max_reenc_times_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.s32MaxReEncodeTimes = %d;\n", max_reenc_times);
                    fputs(str, fp);
                }
                if (frm_max_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32FrmMaxQp = %d;\n", frm_max_qp);
                    fputs(str, fp);
                }
                if (frm_min_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32FrmMinQp = %d;\n", frm_min_qp);
                    fputs(str, fp);
                }
                if (frm_max_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32FrmMaxIQp = %d;\n", frm_max_iqp);
                    fputs(str, fp);
                }
                if (frm_min_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32FrmMinIQp = %d;\n", frm_min_iqp);
                    fputs(str, fp);
                }
                if (md_switch_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH265.u32MotionStaticSwitchFrmQp = %d;\n", md_switch_qp);
                    fputs(str, fp);
                }

                snprintf(str, sizeof(str), "RK_MPI_VENC_SetRcParam(%d, &stRcParam);\n", venc_id);
                fputs(str, fp);
                fputs("}\n\n", fp);
            }
            CLOSE_VENC_SET;
        } else if (type && strcasecmp(type, "h264") == 0) {
            VENC_RC_PARAM_S stRcParam;
            RK_MPI_VENC_GetRcParam(venc_id, &stRcParam);
            if (start_qp_changed) {
                stRcParam.s32FirstFrameStartQp = start_qp;
            }
            if (max_qp_changed) {
                stRcParam.stParamH264.u32MaxQp = max_qp;
            }
            if (min_qp_changed) {
                stRcParam.stParamH264.u32MinQp = min_qp;
            }
            if (max_iqp_changed) {
                stRcParam.stParamH264.u32MaxIQp = max_iqp;
            }
            if (min_iqp_changed) {
                stRcParam.stParamH264.u32MinIQp = min_iqp;
            }
            if (ip_qp_delta_changed) {
                stRcParam.stParamH264.s32DeltIpQp = ip_qp_delta;
            }
            if (max_reenc_times_changed) {
                stRcParam.stParamH264.s32MaxReEncodeTimes = max_reenc_times;
            }
            if (frm_max_qp_changed) {
                stRcParam.stParamH264.u32FrmMaxQp = frm_max_qp;
            }
            if (frm_min_qp_changed) {
                stRcParam.stParamH264.u32FrmMinQp = frm_min_qp;
            }
            if (frm_max_iqp_changed) {
                stRcParam.stParamH264.u32FrmMaxIQp = frm_max_iqp;
            }
            if (frm_min_iqp_changed) {
                stRcParam.stParamH264.u32FrmMinIQp = frm_min_iqp;
            }
            if (md_switch_qp_changed) {
                stRcParam.stParamH264.u32MotionStaticSwitchFrmQp = md_switch_qp;
            }
            RK_MPI_VENC_SetRcParam(venc_id, &stRcParam);

            OPEN_VENC_SET;
            if (fp) {
                char str[1024];
                fputs("{\n", fp);
                snprintf(str, sizeof(str), 
                         "VENC_RC_PARAM_S stRcParam;\n"
                         "RK_MPI_VENC_GetRcParam(%d, &stRcParam);\n", venc_id);
                fputs(str, fp);

                if (start_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.s32FirstFrameStartQp = %d;\n", start_qp);
                    fputs(str, fp);
                }
                if (max_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32MaxQp = %d;\n", max_qp);
                    fputs(str, fp);
                }
                if (min_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32MinQp = %d;\n", min_qp);
                    fputs(str, fp);
                }
                if (max_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32MaxIQp = %d;\n", max_iqp);
                    fputs(str, fp);
                }
                if (min_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32MinIQp = %d;\n", min_iqp);
                    fputs(str, fp);
                }
                if (ip_qp_delta_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.s32DeltIpQp = %d;\n", ip_qp_delta);
                    fputs(str, fp);
                }
                if (max_reenc_times_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.s32MaxReEncodeTimes = %d;\n", max_reenc_times);
                    fputs(str, fp);
                }
                if (frm_max_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32FrmMaxQp = %d;\n", frm_max_qp);
                    fputs(str, fp);
                }
                if (frm_min_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32FrmMinQp = %d;\n", frm_min_qp);
                    fputs(str, fp);
                }
                if (frm_max_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32FrmMaxIQp = %d;\n", frm_max_iqp);
                    fputs(str, fp);
                }
                if (frm_min_iqp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32FrmMinIQp = %d;\n", frm_min_iqp);
                    fputs(str, fp);
                }
                if (md_switch_qp_changed) {
                    snprintf(str, sizeof(str), "stRcParam.stParamH264.u32MotionStaticSwitchFrmQp = %d;\n", md_switch_qp);
                    fputs(str, fp);
                }

                snprintf(str, sizeof(str), "RK_MPI_VENC_SetRcParam(%d, &stRcParam);\n", venc_id);
                fputs(str, fp);
                fputs("}\n\n", fp);
            }
            CLOSE_VENC_SET;
        }
    }
}

void check_and_set_qp_dlt_params(int venc_id, const char* type, const cJSON* venc) {
    int qp_dlt_row_i_changed = 0;
    int qp_dlt_row_changed = 0;
    int thrd_i_changed = 0;
    int thrd_p_changed = 0;
    int step_i_changed = 0;
    int step_p_changed = 0;

    cJSON* qp_dlt_row_i = cJSON_GetObjectItem(venc, "qp_dlt_row_i");
    if (qp_dlt_row_i) {
        if (cJSON_IsNumber(qp_dlt_row_i)) {
            qp_dlt_row_i_changed = 1;
        }
    }

    cJSON* qp_dlt_row = cJSON_GetObjectItem(venc, "qp_dlt_row");
    if (qp_dlt_row) {
        if (cJSON_IsNumber(qp_dlt_row)) {
            qp_dlt_row_changed = 1;
        }
    }

    cJSON* thrd_i = cJSON_GetObjectItem(venc, "thrd_i");
    if (thrd_i) {
        if (cJSON_IsArray(thrd_i)) {
            thrd_i_changed = 1;
        }
    }

    cJSON* thrd_p = cJSON_GetObjectItem(venc, "thrd_p");
    if (thrd_p) {
        if (cJSON_IsArray(thrd_p)) {
            thrd_p_changed = 1;
        }
    }

    cJSON* step_i = cJSON_GetObjectItem(venc, "step_i");
    if (step_i) {
        if (cJSON_IsArray(step_i)) {
            step_i_changed = 1;
        }
    }

    cJSON* step_p = cJSON_GetObjectItem(venc, "step_p");
    if (step_p) {
        if (cJSON_IsArray(step_p)) {
            step_p_changed = 1;
        }
    }

    if (qp_dlt_row_i_changed || qp_dlt_row_changed || thrd_i_changed || thrd_p_changed ||
        step_i_changed || step_p_changed) {
        VENC_RC_PARAM2_S stRcParam2;
        RK_MPI_VENC_GetRcParam2(venc_id, &stRcParam2);
        if (qp_dlt_row_i_changed) {
            stRcParam2.u32RowIQpDelta = qp_dlt_row_i->valueint;
        }
        if (qp_dlt_row_changed) {
            stRcParam2.u32RowQpDelta = qp_dlt_row->valueint;
        }
        if (thrd_i_changed) {
            int size = cJSON_GetArraySize(thrd_i);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(thrd_i, i);
                if (cJSON_IsNumber(item)) {
                    stRcParam2.u32ThrdI[i] = item->valueint;
                }
            }
        }
        if (thrd_p_changed) {
            int size = cJSON_GetArraySize(thrd_p);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(thrd_p, i);
                if (cJSON_IsNumber(item)) {
                    stRcParam2.u32ThrdP[i] = item->valueint;
                }
            }
        }
        if (step_i_changed) {
            int size = cJSON_GetArraySize(step_i);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(step_i, i);
                if (cJSON_IsNumber(item)) {
                    stRcParam2.s32AqStepI[i] = item->valueint;
                }
            }
        }
        if (step_p_changed) {
            int size = cJSON_GetArraySize(step_p);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(step_p, i);
                if (cJSON_IsNumber(item)) {
                    stRcParam2.s32AqStepP[i] = item->valueint;
                }
            }
        }
        RK_MPI_VENC_SetRcParam2(venc_id, &stRcParam2);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_RC_PARAM2_S stRcParam2;\n"
                     "RK_MPI_VENC_GetRcParam2(%d, &stRcParam2);\n", venc_id);
            fputs(str, fp);

            if (qp_dlt_row_i_changed) {
                snprintf(str, sizeof(str), "stRcParam2.u32RowIQpDelta = %d;\n", qp_dlt_row_i->valueint);
                fputs(str, fp);
            }
            if (qp_dlt_row_changed) {
                snprintf(str, sizeof(str), "stRcParam2.u32RowQpDelta = %d;\n", qp_dlt_row->valueint);
                fputs(str, fp);
            }

            if (thrd_i_changed) {
                int size = cJSON_GetArraySize(thrd_i);
                for (int i = 0; i < size && i < 16; i++) {
                    cJSON* item = cJSON_GetArrayItem(thrd_i, i);
                    if (cJSON_IsNumber(item)) {
                        snprintf(str, sizeof(str), "stRcParam2.u32ThrdI[%d] = %d;\n", i, item->valueint);
                        fputs(str, fp);
                    }
                }
            }

            if (thrd_p_changed) {
                int size = cJSON_GetArraySize(thrd_p);
                for (int i = 0; i < size && i < 16; i++) {
                    cJSON* item = cJSON_GetArrayItem(thrd_p, i);
                    if (cJSON_IsNumber(item)) {
                        snprintf(str, sizeof(str), "stRcParam2.u32ThrdP[%d] = %d;\n", i, item->valueint);
                        fputs(str, fp);
                    }
                }
            }

            if (step_i_changed) {
                int size = cJSON_GetArraySize(step_i);
                for (int i = 0; i < size && i < 16; i++) {
                    cJSON* item = cJSON_GetArrayItem(step_i, i);
                    if (cJSON_IsNumber(item)) {
                        snprintf(str, sizeof(str), "stRcParam2.s32AqStepI[%d] = %d;\n", i, item->valueint);
                        fputs(str, fp);
                    }
                }
            }

            if (step_p_changed) {
                int size = cJSON_GetArraySize(step_p);
                for (int i = 0; i < size && i < 16; i++) {
                    cJSON* item = cJSON_GetArrayItem(step_p, i);
                    if (cJSON_IsNumber(item)) {
                        snprintf(str, sizeof(str), "stRcParam2.s32AqStepP[%d] = %d;\n", i, item->valueint);
                        fputs(str, fp);
                    }
                }
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetRcParam2(%d, &stRcParam2);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_aq_rnge_params(int venc_id, const char* type, const cJSON* venc) {
    int aq_rnge_arr_changed = 0;

    cJSON* aq_rnge_arr = cJSON_GetObjectItem(venc, "aq_rnge_arr");
    if (aq_rnge_arr) {
        if (cJSON_IsArray(aq_rnge_arr)) {
            aq_rnge_arr_changed = 1;
        }
    }

    if (aq_rnge_arr_changed) {
#ifndef RV1106_RV1103
        VENC_RC_PARAM3_S stRcParam3;
        RK_MPI_VENC_GetRcParam3(venc_id, &stRcParam3);
        int size = cJSON_GetArraySize(aq_rnge_arr);
        for (int i = 0; i < size; i++) {
            cJSON* item = cJSON_GetArrayItem(aq_rnge_arr, i);
            if (cJSON_IsNumber(item)) {
                stRcParam3.u32AqRange[i] = item->valueint;
            }
        }
        RK_MPI_VENC_SetRcParam3(venc_id, &stRcParam3);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_RC_PARAM3_S stRcParam3;\n"
                     "RK_MPI_VENC_GetRcParam3(%d, &stRcParam3);\n", venc_id);
            fputs(str, fp);

            int size = cJSON_GetArraySize(aq_rnge_arr);
            for (int i = 0; i < size && i < 16; i++) {
                cJSON* item = cJSON_GetArrayItem(aq_rnge_arr, i);
                if (cJSON_IsNumber(item)) {
                    snprintf(str, sizeof(str), "stRcParam3.u32AqRange[%d] = %d;\n", i, item->valueint);
                    fputs(str, fp);
                }
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetRcParam3(%d, &stRcParam3);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
#endif
    }
}

void check_and_set_scene_mode(int venc_id, const char* type, const cJSON* venc) {
    int scene_mode_changed = 0;
    int scene_mode_value = -1;

    cJSON* scene_mode = cJSON_GetObjectItem(venc, "scene_mode");
    if (scene_mode) {
        if (cJSON_IsNumber(scene_mode)) {
            scene_mode_value = scene_mode->valueint;
            scene_mode_changed = 1;
        }
    }

    if (scene_mode_changed) {
        VENC_SCENE_MODE_E scene_mode_enum;
        if (scene_mode_value == 0) {
            scene_mode_enum = SCENE_1;
        } else if (scene_mode_value == 1) {
            scene_mode_enum = SCENE_0;
        } else if (scene_mode_value == 2) {
            scene_mode_enum = SCENE_3;
        } else {
            scene_mode_enum = SCENE_1;
        }
        RK_MPI_VENC_SetSceneMode(venc_id, scene_mode_enum);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];

            fputs("{\n", fp);
            if (scene_mode_value == 0) {
                snprintf(str, sizeof(str), "RK_MPI_VENC_SetSceneMode(%d, SCENE_1);\n", venc_id);
            } else if (scene_mode_value == 1) {
                snprintf(str, sizeof(str), "RK_MPI_VENC_SetSceneMode(%d, SCENE_0);\n", venc_id);
            } else if (scene_mode_value == 2) {
                snprintf(str, sizeof(str), "RK_MPI_VENC_SetSceneMode(%d, SCENE_3);\n", venc_id);
            } else {
                snprintf(str, sizeof(str), "RK_MPI_VENC_SetSceneMode(%d, SCENE_1);\n", venc_id);
            }

            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_flt_str_params(int venc_id, const char* type, const cJSON* venc) {
    int flt_str_i_changed = 0;
    int flt_str_p_changed = 0;
    int flt_str_i_value = -1;
    int flt_str_p_value = -1;

    cJSON* flt_str_i = cJSON_GetObjectItem(venc, "flt_str_i");
    if (flt_str_i) {
        if (cJSON_IsNumber(flt_str_i)) {
            flt_str_i_value = flt_str_i->valueint;
            flt_str_i_changed = 1;
        }
    }

    cJSON* flt_str_p = cJSON_GetObjectItem(venc, "flt_str_p");
    if (flt_str_p) {
        if (cJSON_IsNumber(flt_str_p)) {
            flt_str_p_value = flt_str_p->valueint;
            flt_str_p_changed = 1;
        }
    }

    if (flt_str_i_changed || flt_str_p_changed) {
        VENC_FILTER_S stFilter;
        RK_MPI_VENC_GetFilter(venc_id, &stFilter);
        if (flt_str_i_changed) {
            stFilter.u32StrengthI = flt_str_i_value;
        }
        if (flt_str_p_changed) {
            stFilter.u32StrengthP = flt_str_p_value;
        }
        RK_MPI_VENC_SetFilter(venc_id, &stFilter);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_FILTER_S stFilter;\n"
                     "RK_MPI_VENC_GetFilter(%d, &stFilter);\n", venc_id);
            fputs(str, fp);

            if (flt_str_i_changed) {
                snprintf(str, sizeof(str), "stFilter.u32StrengthI = %d;\n", flt_str_i_value);
                fputs(str, fp);
            }
            if (flt_str_p_changed) {
                snprintf(str, sizeof(str), "stFilter.u32StrengthP = %d;\n", flt_str_p_value);
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetFilter(%d, &stFilter);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_md_swth_params(int venc_id, const char* type, const cJSON* venc) {
    int md_swth_en_changed = 0;
    int md_swth_en_value = -1;

    cJSON* md_swth_en = cJSON_GetObjectItem(venc, "md_swth_en");
    if (md_swth_en) {
        if (cJSON_IsNumber(md_swth_en)) {
            md_swth_en_value = md_swth_en->valueint;
            md_swth_en_changed = 1;
        }
    }

    if (!md_swth_en_changed) {
        cJSON* md_switch_en = cJSON_GetObjectItem(venc, "md_switch_en");
        if (md_switch_en) {
            if (cJSON_IsNumber(md_switch_en)) {
                md_swth_en_value = md_switch_en->valueint;
                md_swth_en_changed = 1;
            }
        }
    }

    if (md_swth_en_changed) {
        RK_MPI_VENC_EnableMotionStaticSwitch(venc_id, md_swth_en_value ? RK_TRUE : RK_FALSE);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "RK_MPI_VENC_EnableMotionStaticSwitch(%d, %s);\n",
                     venc_id, md_swth_en_value ? "RK_TRUE" : "RK_FALSE");
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

#if defined(RV1103B) || defined(RV1126B)
void check_and_set_inst_br_lvl_params(int venc_id, const char* type, const cJSON* venc) {
    int inst_br_lvl_changed = 0;
    int inst_br_lvl_value = -1;

    cJSON* inst_br_lvl = cJSON_GetObjectItem(venc, "inst_br_lvl");
    if (inst_br_lvl) {
        if (cJSON_IsNumber(inst_br_lvl)) {
            inst_br_lvl_value = inst_br_lvl->valueint;
            inst_br_lvl_changed = 1;
        }
    }

    if (inst_br_lvl_changed) {
        RK_MPI_VENC_SetInstBitrateLevel(venc_id, inst_br_lvl_value);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str),
                     "RK_MPI_VENC_SetInstBitrateLevel(%d, %d);\n",
                     venc_id, inst_br_lvl_value);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}
#endif

void check_and_set_deblur_params(int venc_id, const char* type, const cJSON* venc) {
    int deblur_str_changed = 0;
    int deblur_str_value = -1;

    cJSON* deblur_str = cJSON_GetObjectItem(venc, "deblur_str");
    if (deblur_str) {
        if (cJSON_IsNumber(deblur_str)) {
            deblur_str_value = deblur_str->valueint;
            deblur_str_changed = 1;
        }
    }

    if (deblur_str_changed) {
        RK_MPI_VENC_SetMotionDeblurStrength(venc_id, deblur_str_value);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "RK_MPI_VENC_SetMotionDeblurStrength(%d, %d);\n",
                     venc_id, deblur_str_value);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_atr_params(int venc_id, const char* type, const cJSON* venc) {
    int atr_str_i_changed = 0;
    int atr_str_p_changed = 0;
    int atr_str_i_value = -1;
    int atr_str_p_value = -1;

    cJSON* atr_str_i = cJSON_GetObjectItem(venc, "atr_str_i");
    if (atr_str_i) {
        if (cJSON_IsNumber(atr_str_i)) {
            atr_str_i_value = atr_str_i->valueint;
            atr_str_i_changed = 1;
        }
    }

    cJSON* atr_str_p = cJSON_GetObjectItem(venc, "atr_str_p");
    if (atr_str_p) {
        if (cJSON_IsNumber(atr_str_p)) {
            atr_str_p_value = atr_str_p->valueint;
            atr_str_p_changed = 1;
        }
    }

    if (atr_str_i_changed || atr_str_p_changed) {
        VENC_ANTI_RING_S stAntiRing;
        RK_MPI_VENC_GetAntiRing(venc_id, &stAntiRing);
        if (atr_str_i_changed) {
#if defined(RV1103B) || defined(RV1126B)
            stAntiRing.u32AntiRingI = atr_str_i_value;
#endif
        }
        if (atr_str_p_changed) {
            stAntiRing.u32AntiRing = atr_str_p_value;
        }
        RK_MPI_VENC_SetAntiRing(venc_id, &stAntiRing);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_ANTI_RING_S stAntiRing;\n"
                     "RK_MPI_VENC_GetAntiRing(%d, &stAntiRing);\n", venc_id);
            fputs(str, fp);

            if (atr_str_i_changed) {
#if defined(RV1103B) || defined(RV1126B)
                snprintf(str, sizeof(str), "stAntiRing.u32AntiRingI = %d;\n", atr_str_i_value);
                fputs(str, fp);
#endif
            }
            if (atr_str_p_changed) {
                snprintf(str, sizeof(str), "stAntiRing.u32AntiRing = %d;\n", atr_str_p_value);
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetAntiRing(%d, &stAntiRing);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_atl_params(int venc_id, const char* type, const cJSON* venc) {
    int atl_str_changed = 0;
    int atl_str_value = -1;

    cJSON* atl_str = cJSON_GetObjectItem(venc, "atl_str");
    if (atl_str) {
        if (cJSON_IsNumber(atl_str)) {
            atl_str_value = atl_str->valueint;
            atl_str_changed = 1;
        }
    }

    if (atl_str_changed) {
        VENC_ANTI_LINE_S stAntiLine;
        RK_MPI_VENC_GetAntiLine(venc_id, &stAntiLine);
        if (atl_str_changed) {
            stAntiLine.u32AntiLine = atl_str_value;
        }
        RK_MPI_VENC_SetAntiLine(venc_id, &stAntiLine);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_ANTI_LINE_S stAntiLine;\n"
                     "RK_MPI_VENC_GetAntiLine(%d, &stAntiLine);\n"
                     "stAntiLine.u32AntiLine = %d;\n"
                     "RK_MPI_VENC_SetAntiLine(%d, &stAntiLine);\n",
                     venc_id, atl_str_value, venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_static_frame_params(int venc_id, const char* type, const cJSON* venc) {
    int static_frm_num_changed = 0;
    int madp16_th_changed = 0;
    int skip16_wgt_changed = 0;
    int skip32_wgt_changed = 0;

    int static_frm_num_value = -1;
    int madp16_th_value = -1;
    int skip16_wgt_value = -1;
    int skip32_wgt_value = -1;

    cJSON* static_frm_num = cJSON_GetObjectItem(venc, "static_frm_num");
    if (static_frm_num) {
        if (cJSON_IsNumber(static_frm_num)) {
            static_frm_num_value = static_frm_num->valueint;
            static_frm_num_changed = 1;
        }
    }

    cJSON* madp16_th = cJSON_GetObjectItem(venc, "madp16_th");
    if (madp16_th) {
        if (cJSON_IsNumber(madp16_th)) {
            madp16_th_value = madp16_th->valueint;
            madp16_th_changed = 1;
        }
    }

    cJSON* skip16_wgt = cJSON_GetObjectItem(venc, "skip16_wgt");
    if (skip16_wgt) {
        if (cJSON_IsNumber(skip16_wgt)) {
            skip16_wgt_value = skip16_wgt->valueint;
            skip16_wgt_changed = 1;
        }
    }

    cJSON* skip32_wgt = cJSON_GetObjectItem(venc, "skip32_wgt");
    if (skip32_wgt) {
        if (cJSON_IsNumber(skip32_wgt)) {
            skip32_wgt_value = skip32_wgt->valueint;
            skip32_wgt_changed = 1;
        }
    }

    if (static_frm_num_changed || madp16_th_changed || skip16_wgt_changed || skip32_wgt_changed) {
#ifndef RV1106_RV1103
        VENC_STATIC_WEIGHT_S stStaticWeight;
        RK_MPI_VENC_GetStaticWeight(venc_id, &stStaticWeight);
        if (static_frm_num_changed) {
            stStaticWeight.u32FrmNum = static_frm_num_value;
        }
        if (madp16_th_changed) {
            stStaticWeight.u32Madp16Th = madp16_th_value;
        }
        if (skip16_wgt_changed) {
            stStaticWeight.u32Skip16Weight = skip16_wgt_value;
        }
        if (skip32_wgt_changed) {
            stStaticWeight.u32Skip32Weight = skip32_wgt_value;
        }
        RK_MPI_VENC_SetStaticWeight(venc_id, &stStaticWeight);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_STATIC_WEIGHT_S stStaticWeight;\n"
                     "RK_MPI_VENC_GetStaticWeight(%d, &stStaticWeight);\n", venc_id);
            fputs(str, fp);

            if (static_frm_num_changed) {
                snprintf(str, sizeof(str), "stStaticWeight.u32FrmNum = %d;\n", static_frm_num_value);
                fputs(str, fp);
            }
            if (madp16_th_changed) {
                snprintf(str, sizeof(str), "stStaticWeight.u32Madp16Th = %d;\n", madp16_th_value);
                fputs(str, fp);
            }
            if (skip16_wgt_changed) {
                snprintf(str, sizeof(str), "stStaticWeight.u32Skip16Weight = %d;\n", skip16_wgt_value);
                fputs(str, fp);
            }
            if (skip32_wgt_changed) {
                snprintf(str, sizeof(str), "stStaticWeight.u32Skip32Weight = %d;\n", skip32_wgt_value);
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetStaticWeight(%d, &stStaticWeight);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
#endif
    }
}

void check_and_set_lgt_chg_params(int venc_id, const char* type, const cJSON* venc) {
    int lgt_chg_lvl_changed = 0;
    int lgt_chg_lvl_value = -1;

    cJSON* lgt_chg_lvl = cJSON_GetObjectItem(venc, "lgt_chg_lvl");
    if (lgt_chg_lvl) {
        if (cJSON_IsNumber(lgt_chg_lvl)) {
            lgt_chg_lvl_value = lgt_chg_lvl->valueint;
            lgt_chg_lvl_changed = 1;
        }
    }

    if (lgt_chg_lvl_changed) {
#ifndef RV1106_RV1103
        VENC_LIGHT_CHANGE_S stLightChange;
        RK_MPI_VENC_GetLightChange(venc_id, &stLightChange);
        stLightChange.u32Level = lgt_chg_lvl_value;
        RK_MPI_VENC_SetLightChange(venc_id, &stLightChange);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_LIGHT_CHANGE_S stLightChange;\n"
                     "RK_MPI_VENC_GetLightChange(%d, &stLightChange);\n"
                     "stLightChange.u32Level = %d;\n"
                     "RK_MPI_VENC_SetLightChange(%d, &stLightChange);\n",
                     venc_id, lgt_chg_lvl_value, venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
#endif
    }
}

void check_and_set_dblk_mode_params(int venc_id, const char* type, const cJSON* venc) {
    int dblk_mode_changed = 0;
    int alpha_changed = 0;
    int beta_changed = 0;

    int dblk_mode_value = -1;
    int alpha_value = -1;
    int beta_value = -1;

    cJSON* dblk_mode = cJSON_GetObjectItem(venc, "DblkMode");
    if (dblk_mode) {
        if (cJSON_IsNumber(dblk_mode)) {
            dblk_mode_value = dblk_mode->valueint;
            dblk_mode_changed = 1;
        }
    }

    cJSON* alpha = cJSON_GetObjectItem(venc, "alpha");
    if (alpha) {
        if (cJSON_IsNumber(alpha)) {
            alpha_value = alpha->valueint;
            alpha_changed = 1;
        }
    }

    cJSON* beta = cJSON_GetObjectItem(venc, "beta");
    if (beta) {
        if (cJSON_IsNumber(beta)) {
            beta_value = beta->valueint;
            beta_changed = 1;
        }
    }

    if (dblk_mode_changed || alpha_changed || beta_changed) {
        VENC_H264_DBLK_S stH264Dblk;
        RK_MPI_VENC_GetH264Dblk(venc_id, &stH264Dblk);
        if (dblk_mode_changed) {
            stH264Dblk.disable_deblocking_filter_idc = !dblk_mode_value;
        }
        if (alpha_changed) {
            stH264Dblk.slice_alpha_c0_offset_div2 = alpha_value;
        }
        if (beta_changed) {
            stH264Dblk.slice_beta_offset_div2 = beta_value;
        }
        RK_MPI_VENC_SetH264Dblk(venc_id, &stH264Dblk);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_H264_DBLK_S stH264Dblk;\n"
                     "RK_MPI_VENC_GetH264Dblk(%d, &stH264Dblk);\n", venc_id);
            fputs(str, fp);

            if (dblk_mode_changed) {
                snprintf(str, sizeof(str), "stH264Dblk.disable_deblocking_filter_idc = %d;\n", !dblk_mode_value);
                fputs(str, fp);
            }
            if (alpha_changed) {
                snprintf(str, sizeof(str), "stH264Dblk.slice_alpha_c0_offset_div2 = %d;\n", alpha_value);
                fputs(str, fp);
            }
            if (beta_changed) {
                snprintf(str, sizeof(str), "stH264Dblk.slice_beta_offset_div2 = %d;\n", beta_value);
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetH264Dblk(%d, &stH264Dblk);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_dblk_en_params(int venc_id, const char* type, const cJSON* venc) {
    int dblk_en_changed = 0;
    int tc_changed = 0;
    int Beta_changed = 0;

    int dblk_en_value = -1;
    int tc_value = -1;
    int Beta_value = -1;

    cJSON* dblk_en = cJSON_GetObjectItem(venc, "DblkEn");
    if (dblk_en) {
        if (cJSON_IsString(dblk_en)) {
            if (strcasecmp(dblk_en->valuestring, "Y") == 0)
                dblk_en_value = 1;
            else
                dblk_en_value = 0;
            dblk_en_changed = 1;
        }
    }

    cJSON* tc = cJSON_GetObjectItem(venc, "Tc");
    if (tc) {
        if (cJSON_IsNumber(tc)) {
            tc_value = tc->valueint;
            tc_changed = 1;
        }
    }

    cJSON* Beta = cJSON_GetObjectItem(venc, "Beta");
    if (Beta) {
        if (cJSON_IsNumber(Beta)) {
            Beta_value = Beta->valueint;
            Beta_changed = 1;
        }
    }

    if (dblk_en_changed || tc_changed || Beta_changed) {
        VENC_H265_DBLK_S stH265Dblk;
        RK_MPI_VENC_SetH265Dblk(venc_id, &stH265Dblk);
        if (dblk_en_changed) {
            stH265Dblk.slice_deblocking_filter_disabled_flag = !dblk_en_value;
        }
        if (tc_changed) {
            stH265Dblk.slice_tc_offset_div2 = tc_value;
        }
        if (Beta_changed) {
            stH265Dblk.slice_beta_offset_div2 = Beta_value;
        }
        RK_MPI_VENC_GetH265Dblk(venc_id, &stH265Dblk);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_H265_DBLK_S stH265Dblk;\n"
                     "RK_MPI_VENC_GetH265Dblk(%d, &stH265Dblk);\n", venc_id);
            fputs(str, fp);

            if (dblk_en_changed) {
                snprintf(str, sizeof(str), "stH265Dblk.slice_deblocking_filter_disabled_flag = %d;\n", !dblk_en_value);
                fputs(str, fp);
            }
            if (tc_changed) {
                snprintf(str, sizeof(str), "stH265Dblk.slice_tc_offset_div2 = %d;\n", tc_value);
                fputs(str, fp);
            }
            if (Beta_changed) {
                snprintf(str, sizeof(str), "stH265Dblk.slice_beta_offset_div2 = %d;\n", Beta_value);
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetH265Dblk(%d, &stH265Dblk);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }

}

void check_and_set_atf_params(int venc_id, const char* type, const cJSON* venc) {
    int atf_str_changed = 0;
    int atf_str_value = -1;

    cJSON* atf_str = cJSON_GetObjectItem(venc, "atf_str");
    if (atf_str) {
        if (cJSON_IsNumber(atf_str)) {
            atf_str_value = atf_str->valueint;
            atf_str_changed = 1;
        }
    }

    if (atf_str_changed) {
#ifndef RV1106_RV1103
        VENC_ANTI_FLICK_S stAntiFlick;
        RK_MPI_VENC_GetAntiFlick(venc_id, &stAntiFlick);
        stAntiFlick.u32AntiFlick = atf_str_value;
        RK_MPI_VENC_SetAntiFlick(venc_id, &stAntiFlick);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_ANTI_FLICK_S stAntiFlick;\n"
                     "RK_MPI_VENC_GetAntiFlick(%d, &stAntiFlick);\n"
                     "stAntiFlick.u32AntiFlick = %d;\n"
                     "RK_MPI_VENC_SetAntiFlick(%d, &stAntiFlick);\n",
                     venc_id, atf_str_value, venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
#endif
    }
}

void check_and_set_lambda_idx_params(int venc_id, const char* type, const cJSON* venc) {
    int lambda_idx_p_changed = 0;
    int lambda_idx_i_changed = 0;
    int lambda_idx_p_value = -1;
    int lambda_idx_i_value = -1;

    cJSON* lambda_idx_p = cJSON_GetObjectItem(venc, "lambda_idx_p");
    if (lambda_idx_p) {
        if (cJSON_IsNumber(lambda_idx_p)) {
            lambda_idx_p_value = lambda_idx_p->valueint;
            lambda_idx_p_changed = 1;
        }
    }

    cJSON* lambda_idx_i = cJSON_GetObjectItem(venc, "lambda_idx_i");
    if (lambda_idx_i) {
        if (cJSON_IsNumber(lambda_idx_i)) {
            lambda_idx_i_value = lambda_idx_i->valueint;
            lambda_idx_i_changed = 1;
        }
    }

    if (lambda_idx_p_changed || lambda_idx_i_changed) {
        VENC_LAMBDA_S stLambda;
        RK_MPI_VENC_GetLambda(venc_id, &stLambda);
        if (lambda_idx_p_changed)
            stLambda.u32Lambda = lambda_idx_p_value;
#ifndef RV1106_RV1103
        if (lambda_idx_i_changed)
            stLambda.u32LambdaI = lambda_idx_i_value;
#endif
        RK_MPI_VENC_SetLambda(venc_id, &stLambda);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_LAMBDA_S stLambda;\n"
                     "RK_MPI_VENC_GetLambda(%d, &stLambda);\n", venc_id);
            fputs(str, fp);

            if (lambda_idx_p_changed) {
                snprintf(str, sizeof(str), "stLambda.u32Lambda = %d;\n", lambda_idx_p_value);
                fputs(str, fp);
            }
            if (lambda_idx_i_changed) {
                snprintf(str, sizeof(str), "stLambda.u32LambdaI = %d;\n", lambda_idx_i_value);
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetLambda(%d, &stLambda);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

#ifdef SMART_V3
void check_and_set_smt3_en_params(int venc_id, const char* type, const cJSON* venc) {
    int smt3_en_changed = 0;
    int se_mode_changed = 0;
    int smt3_en_value = -1;
    int se_mode_value = -1;

    cJSON* smt3_en = cJSON_GetObjectItem(venc, "smt3_en");
    if (smt3_en) {
        if (cJSON_IsNumber(smt3_en)) {
            smt3_en_value = smt3_en->valueint;
            smt3_en_changed = 1;
        }
    }

    cJSON* se_mode = cJSON_GetObjectItem(venc, "se_mode");
    if (se_mode) {
        if (cJSON_IsNumber(se_mode)) {
            se_mode_value = se_mode->valueint;
            se_mode_changed = 1;
        }
    }

    if (smt3_en_changed || se_mode_changed) {
        if (smt3_en_value == 1) {
            RK_MPI_VENC_EnableSvc(venc_id, RK_FALSE);
            RK_MPI_VENC_EnableSvc3(venc_id, RK_TRUE, se_mode_value, NULL, NULL);
        } else if (smt3_en_value == 0) {
            RK_MPI_VENC_EnableSvc3(venc_id, RK_FALSE, 1, NULL, NULL);
        }

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);

            if (smt3_en_value == 1) {
                snprintf(str, sizeof(str),
                         "RK_MPI_VENC_EnableSvc(%d, RK_FALSE);\n"
                         "RK_MPI_VENC_EnableSvc3(%d, RK_TRUE, %d, NULL, NULL);\n",
                         venc_id, venc_id, se_mode_value);
            } else if (smt3_en_value == 0) {
                snprintf(str, sizeof(str),
                         "RK_MPI_VENC_EnableSvc3(%d, RK_FALSE, 1, NULL, NULL);\n",
                         venc_id);
            }

            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}
#endif

void check_and_set_smt1_en_params(int venc_id, const char* type, const cJSON* venc) {
    int smt1_en_changed = 0;
    int smt1_en_value = -1;

    cJSON* smt1_en = cJSON_GetObjectItem(venc, "smt1_en");
    if (smt1_en) {
        if (cJSON_IsNumber(smt1_en)) {
            smt1_en_value = smt1_en->valueint;
            smt1_en_changed = 1;
        }
    }

    if (smt1_en_changed) {
        if (smt1_en_value == 1) {
#ifdef SMART_V3
            RK_MPI_VENC_EnableSvc3(venc_id, RK_FALSE, 1, NULL, NULL);
#endif
            RK_MPI_VENC_EnableSvc(venc_id, RK_TRUE);
        } else if (smt1_en_value == 0) {
            RK_MPI_VENC_EnableSvc(venc_id, RK_FALSE);
        }

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);

            if (smt1_en_value == 1) {
#ifdef SMART_V3
                snprintf(str, sizeof(str),
                         "RK_MPI_VENC_EnableSvc3(%d, RK_FALSE, 1, NULL, NULL);\n"
                         "RK_MPI_VENC_EnableSvc(%d, RK_TRUE);\n",
                         venc_id, venc_id);
#else
                snprintf(str, sizeof(str),
                         "RK_MPI_VENC_EnableSvc(%d, RK_TRUE);\n",
                         venc_id);
#endif
            } else if (smt1_en_value == 0) {
                snprintf(str, sizeof(str),
                         "RK_MPI_VENC_EnableSvc(%d, RK_FALSE);\n",
                         venc_id);
            }

            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_sao_params(int venc_id, const char* type, const cJSON* venc) {
    int saoluma_changed = 0;
    int saochroma_changed = 0;
    int sao_bit_ratio_changed = 0;
    int sao_str_i_changed = 0;
    int sao_str_p_changed = 0;

    int saoluma_value = -1;
    int saochroma_value = -1;
    int sao_bit_ratio_value = -1;
    int sao_str_i_value = -1;
    int sao_str_p_value = -1;

    cJSON* saoluma = cJSON_GetObjectItem(venc, "Saoluma");
    if (saoluma) {
        if (cJSON_IsNumber(saoluma)) {
            saoluma_value = saoluma->valueint;
            saoluma_changed = 1;
        }
    }

    cJSON* saochroma = cJSON_GetObjectItem(venc, "Saochroma");
    if (saochroma) {
        if (cJSON_IsNumber(saochroma)) {
            saochroma_value = saochroma->valueint;
            saochroma_changed = 1;
        }
    }

    cJSON* sao_bit_ratio = cJSON_GetObjectItem(venc, "SaoBitRatio");
    if (sao_bit_ratio) {
        if (cJSON_IsNumber(sao_bit_ratio)) {
            sao_bit_ratio_value = sao_bit_ratio->valueint;
            sao_bit_ratio_changed = 1;
        }
    }

    cJSON* sao_str_i = cJSON_GetObjectItem(venc, "sao_str_i");
    if (sao_str_i) {
        if (cJSON_IsNumber(sao_str_i)) {
            sao_str_i_value = sao_str_i->valueint;
            sao_str_i_changed = 1;
        }
    }

    cJSON* sao_str_p = cJSON_GetObjectItem(venc, "sao_str_p");
    if (sao_str_p) {
        if (cJSON_IsNumber(sao_str_p)) {
            sao_str_p_value = sao_str_p->valueint;
            sao_str_p_changed = 1;
        }
    }

    if (saoluma_changed || saochroma_changed || sao_bit_ratio_changed ||
        sao_str_i_changed || sao_str_p_changed) {
        VENC_H265_SAO_S stH265Sao;
        memset(&stH265Sao, 0, sizeof(stH265Sao));
        RK_MPI_VENC_GetH265Sao(venc_id, &stH265Sao);
        if (saoluma_changed) {
            stH265Sao.slice_sao_luma_flag = saoluma_value;
        }
        if (saochroma_changed) {
            stH265Sao.slice_sao_chroma_flag = saochroma_value;
        }
        if (sao_bit_ratio_changed) {
            stH265Sao.slice_sao_bit_ratio = sao_bit_ratio_value;
        }
#if defined(RV1103B) || defined(RV1126B)
        if (sao_str_i_changed) {
            stH265Sao.u32SaoStrengthI = sao_str_i_value;
        }
        if (sao_str_p_changed) {
            stH265Sao.u32SaoStrengthP = sao_str_p_value;
        }
#endif
        RK_MPI_VENC_SetH265Sao(venc_id, &stH265Sao);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_H265_SAO_S stH265Sao;\n"
                     "memset(&stH265Sao, 0, sizeof(stH265Sao));\n"
                     "RK_MPI_VENC_GetH265Sao(%d, &stH265Sao);\n", venc_id);
            fputs(str, fp);

            if (saoluma_changed) {
                snprintf(str, sizeof(str), "stH265Sao.slice_sao_luma_flag = %d;\n", saoluma_value);
                fputs(str, fp);
            }
            if (saochroma_changed) {
                snprintf(str, sizeof(str), "stH265Sao.slice_sao_chroma_flag = %d;\n", saochroma_value);
                fputs(str, fp);
            }
            if (sao_bit_ratio_changed) {
                snprintf(str, sizeof(str), "stH265Sao.slice_sao_bit_ratio = %d;\n", sao_bit_ratio_value);
                fputs(str, fp);
            }
#if defined(RV1103B) || defined(RV1126B)
            if (sao_str_i_changed) {
                snprintf(str, sizeof(str), "stH265Sao.u32SaoStrengthI = %d;\n", sao_str_i_value);
                fputs(str, fp);
            }
            if (sao_str_p_changed) {
                snprintf(str, sizeof(str), "stH265Sao.u32SaoStrengthP = %d;\n", sao_str_p_value);
                fputs(str, fp);
            }
#endif

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetH265Sao(%d, &stH265Sao);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_intra_smoothing_params(int venc_id, const char* type, const cJSON* venc) {
    int intra_smoothing_changed = 0;
    int intra_smoothing_value = -1;

    cJSON* intra_smoothing = cJSON_GetObjectItem(venc, "IntraSmoothing");
    if (intra_smoothing) {
        if (cJSON_IsNumber(intra_smoothing)) {
            intra_smoothing_value = intra_smoothing->valueint;
            intra_smoothing_changed = 1;
        }
    }

    if (intra_smoothing_changed) {
        VENC_H265_PU_S stPredUnit;
        RK_MPI_VENC_GetH265PredUnit(venc_id, &stPredUnit);
        stPredUnit.strong_intra_smoothing_enabled_flag = intra_smoothing_value;
        RK_MPI_VENC_SetH265PredUnit(venc_id, &stPredUnit);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_H265_PU_S stPredUnit;\n"
                     "RK_MPI_VENC_GetH265PredUnit(%d, &stPredUnit);\n"
                     "stPredUnit.strong_intra_smoothing_enabled_flag = %d;\n"
                     "RK_MPI_VENC_SetH265PredUnit(%d, &stPredUnit);\n",
                     venc_id, intra_smoothing_value, venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_tmvp_en_params(int venc_id, const char* type, const cJSON* venc) {
    int tmvp_en_changed = 0;
    int tmvp_en_value = -1;

    cJSON* tmvp_en = cJSON_GetObjectItem(venc, "TmvpEn");
    if (tmvp_en) {
        if (cJSON_IsNumber(tmvp_en)) {
            tmvp_en_value = tmvp_en->valueint;
            tmvp_en_changed = 1;
        }
    }

    if (tmvp_en_changed) {
#ifndef RV1106_RV1103
        RK_MPI_VENC_EnableTmvp(venc_id, tmvp_en_value ? RK_TRUE : RK_FALSE);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), "RK_MPI_VENC_EnableTmvp(%d, %d);\n", 
                     venc_id, tmvp_en_value ? RK_TRUE : RK_FALSE);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
#endif
    }
}

void check_and_set_trans_scaling_params(int venc_id, const char* type, const cJSON* venc) {
    int trans8_changed = 0;
    int scaling_mode_changed = 0;
    int cb_qp_offset_changed = 0;
    int cr_qp_offset_changed = 0;

    int trans8_value = -1;
    int scaling_mode_value = -1;
    int cb_qp_offset_value = -100;  // 使用-100作为无效值，因为QP偏移可能为负
    int cr_qp_offset_value = -100;

    cJSON* trans8 = cJSON_GetObjectItem(venc, "trans8");
    if (trans8) {
        if (cJSON_IsNumber(trans8)) {
            trans8_value = trans8->valueint;
            trans8_changed = 1;
        }
    }

    cJSON* scaling_mode = cJSON_GetObjectItem(venc, "ScalingMode");
    if (scaling_mode) {
        if (cJSON_IsNumber(scaling_mode)) {
            scaling_mode_value = scaling_mode->valueint;
            scaling_mode_changed = 1;
        }
    }

    cJSON* cb_qp_offset = cJSON_GetObjectItem(venc, "CbQpOffset");
    if (cb_qp_offset) {
        if (cJSON_IsNumber(cb_qp_offset)) {
            cb_qp_offset_value = cb_qp_offset->valueint;
            cb_qp_offset_changed = 1;
        }
    }

    cJSON* cr_qp_offset = cJSON_GetObjectItem(venc, "CrQpOffset");
    if (cr_qp_offset) {
        if (cJSON_IsNumber(cr_qp_offset)) {
            cr_qp_offset_value = cr_qp_offset->valueint;
            cr_qp_offset_changed = 1;
        }
    }

    if (trans8_changed || scaling_mode_changed || cb_qp_offset_changed || cr_qp_offset_changed) {
        if (type && strcasecmp(type, "h265") == 0) {
            VENC_H265_TRANS_S stH265Trans;
            RK_MPI_VENC_GetH265Trans(venc_id, &stH265Trans);
            if (scaling_mode_changed) {
                stH265Trans.bScalingListEnabled = scaling_mode_value;
            }
            if (cb_qp_offset_changed) {
                stH265Trans.cb_qp_offset = cb_qp_offset_value;
            }
            if (cr_qp_offset_changed) {
                stH265Trans.cr_qp_offset = cr_qp_offset_value;
            }
            RK_MPI_VENC_SetH265Trans(venc_id, &stH265Trans);

            OPEN_VENC_SET;
            if (fp) {
                char str[1024];
                fputs("{\n", fp);
                snprintf(str, sizeof(str), 
                         "VENC_H265_TRANS_S stH265Trans;\n"
                         "RK_MPI_VENC_GetH265Trans(%d, &stH265Trans);\n", venc_id);
                fputs(str, fp);

                if (scaling_mode_changed) {
                    snprintf(str, sizeof(str), "stH265Trans.bScalingListEnabled = %d;\n", scaling_mode_value);
                    fputs(str, fp);
                }
                if (cb_qp_offset_changed) {
                    snprintf(str, sizeof(str), "stH265Trans.cb_qp_offset = %d;\n", cb_qp_offset_value);
                    fputs(str, fp);
                }
                if (cr_qp_offset_changed) {
                    snprintf(str, sizeof(str), "stH265Trans.cr_qp_offset = %d;\n", cr_qp_offset_value);
                    fputs(str, fp);
                }

                snprintf(str, sizeof(str), "RK_MPI_VENC_SetH265Trans(%d, &stH265Trans);\n", venc_id);
                fputs(str, fp);
                fputs("}\n\n", fp);
            }
            CLOSE_VENC_SET;
        } else if (type && strcasecmp(type, "h264") == 0) {
            VENC_H264_TRANS_S stH264Trans;
            RK_MPI_VENC_GetH264Trans(venc_id, &stH264Trans);
            if (trans8_changed) {
                stH264Trans.u32TransMode = trans8_value;
            }
            if (scaling_mode_changed) {
                stH264Trans.bScalingListValid = scaling_mode_value;
            }
            if (cb_qp_offset_changed) {
                stH264Trans.chroma_qp_index_offset = cb_qp_offset_value;
            }
            if (cr_qp_offset_changed) {
                stH264Trans.chroma_qp_index_offset = cr_qp_offset_value;
            }
            RK_MPI_VENC_SetH264Trans(venc_id, &stH264Trans);

            OPEN_VENC_SET;
            if (fp) {
                char str[1024];
                fputs("{\n", fp);
                snprintf(str, sizeof(str), 
                         "VENC_H264_TRANS_S stH264Trans;\n"
                         "RK_MPI_VENC_GetH264Trans(%d, &stH264Trans);\n", venc_id);
                fputs(str, fp);

                if (trans8_changed) {
                    snprintf(str, sizeof(str), "stH264Trans.u32TransMode = %d;\n", trans8_value);
                    fputs(str, fp);
                }
                if (scaling_mode_changed) {
                    snprintf(str, sizeof(str), "stH264Trans.bScalingListValid = %d;\n", scaling_mode_value);
                    fputs(str, fp);
                }
                if (cb_qp_offset_changed) {
                    snprintf(str, sizeof(str), "stH264Trans.chroma_qp_index_offset = %d;\n", cb_qp_offset_value);
                    fputs(str, fp);
                }
                if (cr_qp_offset_changed) {
                    snprintf(str, sizeof(str), "stH264Trans.chroma_qp_index_offset = %d;\n", cr_qp_offset_value);
                    fputs(str, fp);
                }

                snprintf(str, sizeof(str), "RK_MPI_VENC_SetH264Trans(%d, &stH264Trans);\n", venc_id);
                fputs(str, fp);
                fputs("}\n\n", fp);
            }
            CLOSE_VENC_SET;
        }
    }
}

void check_and_set_gop_params(int venc_id, const char* type, const cJSON* venc) {
    int stat_tm_changed = 0;
    int gop_changed = 0;
    int gop_mode_changed = 0;

    int stat_tm_value = -1;
    int gop_value = -1;
    const char* gop_mode_value = NULL;

    cJSON* stat_tm = cJSON_GetObjectItem(venc, "StatTm");
    if (stat_tm) {
        if (cJSON_IsNumber(stat_tm)) {
            stat_tm_value = stat_tm->valueint;
            stat_tm_changed = 1;
        }
    }

    cJSON* gop = cJSON_GetObjectItem(venc, "Gop");
    if (gop) {
        if (cJSON_IsNumber(gop)) {
            gop_value = gop->valueint;
            gop_changed = 1;
        }
    }

    cJSON* gop_mode = cJSON_GetObjectItem(venc, "GopMode");
    if (gop_mode) {
        if (cJSON_IsString(gop_mode)) {
            gop_mode_value = gop_mode->valuestring;
            gop_mode_changed = 1;
        }
    }

    if (stat_tm_changed || gop_changed || gop_mode_changed) {
        VENC_CHN_ATTR_S stChnAttr;
        RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
        if (stat_tm_changed) {
            switch (stChnAttr.stRcAttr.enRcMode) {
            case VENC_RC_MODE_H264CBR:
                stChnAttr.stRcAttr.stH264Cbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_H264VBR:
                stChnAttr.stRcAttr.stH264Vbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_H264AVBR:
                stChnAttr.stRcAttr.stH264Avbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_MJPEGCBR:
                stChnAttr.stRcAttr.stMjpegCbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_MJPEGVBR:
                stChnAttr.stRcAttr.stMjpegVbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_H265CBR:
                stChnAttr.stRcAttr.stH265Cbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_H265VBR:
                stChnAttr.stRcAttr.stH265Vbr.u32StatTime = stat_tm_value;
                break;
            case VENC_RC_MODE_H265AVBR:
                stChnAttr.stRcAttr.stH265Avbr.u32StatTime = stat_tm_value;
                break;
            default:
                break;
            }
        }
        if (gop_changed) {
            switch (stChnAttr.stRcAttr.enRcMode) {
            case VENC_RC_MODE_H264CBR:
                stChnAttr.stRcAttr.stH264Cbr.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H264VBR:
                stChnAttr.stRcAttr.stH264Vbr.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H264AVBR:
                stChnAttr.stRcAttr.stH264Avbr.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H264FIXQP:
                stChnAttr.stRcAttr.stH264FixQp.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H265CBR:
                stChnAttr.stRcAttr.stH265Cbr.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H265VBR:
                stChnAttr.stRcAttr.stH265Vbr.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H265AVBR:
                stChnAttr.stRcAttr.stH265Avbr.u32Gop = gop_value;
                break;
            case VENC_RC_MODE_H265FIXQP:
                stChnAttr.stRcAttr.stH265FixQp.u32Gop = gop_value;
                break;
            default:
                break;
            }
        }
        if (gop_mode_changed) {
            if (strcasecmp(gop_mode_value, "normalp") == 0) {
                stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
            } else if (strcasecmp(gop_mode_value, "smartp") == 0) {
                stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
            } else if (strcasecmp(gop_mode_value, "tsvc2") == 0) {
                stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC2;
            } else if (strcasecmp(gop_mode_value, "tsvc3") == 0) {
                stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC3;
            } else if (strcasecmp(gop_mode_value, "tsvc4") == 0) {
                stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;
            } else {
                stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
            }
        }
        RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_CHN_ATTR_S stChnAttr;\n"
                     "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n", venc_id);
            fputs(str, fp);

            if (stat_tm_changed) {
                snprintf(str, sizeof(str), "switch (stChnAttr.stRcAttr.enRcMode) {\n");
                fputs(str, fp);

                snprintf(str, sizeof(str), 
                         "case VENC_RC_MODE_H264CBR:\n"
                         "    stChnAttr.stRcAttr.stH264Cbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H264VBR:\n"
                         "    stChnAttr.stRcAttr.stH264Vbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H264AVBR:\n"
                         "    stChnAttr.stRcAttr.stH264Avbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_MJPEGCBR:\n"
                         "    stChnAttr.stRcAttr.stMjpegCbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_MJPEGVBR:\n"
                         "    stChnAttr.stRcAttr.stMjpegVbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265CBR:\n"
                         "    stChnAttr.stRcAttr.stH265Cbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265VBR:\n"
                         "    stChnAttr.stRcAttr.stH265Vbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265AVBR:\n"
                         "    stChnAttr.stRcAttr.stH265Avbr.u32StatTime = %d;\n"
                         "    break;\n"
                         "default:\n"
                         "    break;\n"
                         "}\n",
                    stat_tm_value, stat_tm_value, stat_tm_value, stat_tm_value,
                    stat_tm_value, stat_tm_value, stat_tm_value, stat_tm_value);
                fputs(str, fp);
            }

            if (gop_changed) {
                snprintf(str, sizeof(str), "switch (stChnAttr.stRcAttr.enRcMode) {\n");
                fputs(str, fp);

                snprintf(str, sizeof(str), 
                         "case VENC_RC_MODE_H264CBR:\n"
                         "    stChnAttr.stRcAttr.stH264Cbr.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H264VBR:\n"
                         "    stChnAttr.stRcAttr.stH264Vbr.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H264AVBR:\n"
                         "    stChnAttr.stRcAttr.stH264Avbr.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H264FIXQP:\n"
                         "    stChnAttr.stRcAttr.stH264FixQp.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265CBR:\n"
                         "    stChnAttr.stRcAttr.stH265Cbr.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265VBR:\n"
                         "    stChnAttr.stRcAttr.stH265Vbr.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265AVBR:\n"
                         "    stChnAttr.stRcAttr.stH265Avbr.u32Gop = %d;\n"
                         "    break;\n"
                         "case VENC_RC_MODE_H265FIXQP:\n"
                         "    stChnAttr.stRcAttr.stH265FixQp.u32Gop = %d;\n"
                         "    break;\n"
                         "default:\n"
                         "    break;\n"
                         "}\n",
                    gop_value, gop_value, gop_value, gop_value,
                    gop_value, gop_value, gop_value, gop_value);
                fputs(str, fp);
            }

            if (gop_mode_changed) {
                if (strcasecmp(gop_mode_value, "normalp") == 0) {
                    snprintf(str, sizeof(str), "stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;\n");
                } else if (strcasecmp(gop_mode_value, "smartp") == 0) {
                    snprintf(str, sizeof(str), "stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;\n");
                } else if (strcasecmp(gop_mode_value, "tsvc2") == 0) {
                    snprintf(str, sizeof(str), "stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC2;\n");
                } else if (strcasecmp(gop_mode_value, "tsvc3") == 0) {
                    snprintf(str, sizeof(str), "stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC3;\n");
                } else if (strcasecmp(gop_mode_value, "tsvc4") == 0) {
                    snprintf(str, sizeof(str), "stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;\n");
                } else {
                    snprintf(str, sizeof(str), "stChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;\n");
                }
                fputs(str, fp);
            }

            snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_rc_mode_bitrate(int venc_id, const char* type, const cJSON* venc) {
    int rc_mode_changed = 0;
    int br_changed = 0;
    int min_br_changed = 0;
    int max_br_changed = 0;

    char* rc_mode_value = NULL;
    int br_value = -1;
    int min_br_value = -1;
    int max_br_value = -1;

    cJSON* rc_mode = cJSON_GetObjectItem(venc, "RcMode");
    if (rc_mode) {
        if (cJSON_IsString(rc_mode)) {
            rc_mode_value = rc_mode->valuestring;
            rc_mode_changed = 1;
        }
    }

    cJSON* br = cJSON_GetObjectItem(venc, "Br(kbps)");
    if (br) {
        if (cJSON_IsNumber(br)) {
            br_value = br->valueint;
            br_changed = 1;
        }
    }

    cJSON* min_br = cJSON_GetObjectItem(venc, "MinBr(kbps)");
    if (min_br) {
        if (cJSON_IsNumber(min_br)) {
            min_br_value = min_br->valueint;
            min_br_changed = 1;
        }
    }

    cJSON* max_br = cJSON_GetObjectItem(venc, "MaxBr(kbps)");
    if (max_br) {
        if (cJSON_IsNumber(max_br)) {
            max_br_value = max_br->valueint;
            max_br_changed = 1;
        }
    }

    if (rc_mode_changed || br_changed || min_br_changed || max_br_changed) {
        if (rc_mode_changed && strcasecmp(rc_mode_value, "cbr") == 0) {
            if (type && strcasecmp(type, "h265") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stH265Cbr.u32BitRate = br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Cbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h264") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stH264Cbr.u32BitRate = br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH264Cbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "mjpeg") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stMjpegCbr.u32BitRate = br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stMjpegCbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            }
        } else if (rc_mode_changed && (strcasecmp(rc_mode_value, "vbr") == 0 || strcasecmp(rc_mode_value, "smart") == 0)) {
            if (type && strcasecmp(type, "h265") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stH265Vbr.u32BitRate = br_value;
                }
                if (min_br_changed) {
                    stChnAttr.stRcAttr.stH265Vbr.u32MinBitRate = min_br_value;
                }
                if (max_br_changed) {
                    stChnAttr.stRcAttr.stH265Vbr.u32MaxBitRate = max_br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Vbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }
                    if (min_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Vbr.u32MinBitRate = %d;\n", min_br_value);
                        fputs(str, fp);
                    }
                    if (max_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Vbr.u32MaxBitRate = %d;\n", max_br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h264") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stH264Vbr.u32BitRate = br_value;
                }
                if (min_br_changed) {
                    stChnAttr.stRcAttr.stH264Vbr.u32MinBitRate = min_br_value;
                }
                if (max_br_changed) {
                    stChnAttr.stRcAttr.stH264Vbr.u32MaxBitRate = max_br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH264Vbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }
                    if (min_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH264Vbr.u32MinBitRate = %d;\n", min_br_value);
                        fputs(str, fp);
                    }
                    if (max_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH264Vbr.u32MaxBitRate = %d;\n", max_br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "mjpeg") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGVBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stMjpegVbr.u32BitRate = br_value;
                }
                if (min_br_changed) {
                    stChnAttr.stRcAttr.stMjpegVbr.u32MinBitRate = min_br_value;
                }
                if (max_br_changed) {
                    stChnAttr.stRcAttr.stMjpegVbr.u32MaxBitRate = max_br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGVBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stMjpegVbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }
                    if (min_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stMjpegVbr.u32MinBitRate = %d;\n", min_br_value);
                        fputs(str, fp);
                    }
                    if (max_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stMjpegVbr.u32MaxBitRate = %d;\n", max_br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            }
        } else if (rc_mode_changed && strcasecmp(rc_mode_value, "avbr") == 0) {
            if (type && strcasecmp(type, "h265") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265AVBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stH265Avbr.u32BitRate = br_value;
                }
                if (min_br_changed) {
                    stChnAttr.stRcAttr.stH265Avbr.u32MinBitRate = min_br_value;
                }
                if (max_br_changed) {
                    stChnAttr.stRcAttr.stH265Avbr.u32MaxBitRate = max_br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265AVBR;\n", venc_id);
                    fputs(str, fp);

                    if (br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Avbr.u32BitRate = %d;\n", br_value);
                        fputs(str, fp);
                    }
                    if (min_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Avbr.u32MinBitRate = %d;\n", min_br_value);
                        fputs(str, fp);
                    }
                    if (max_br_changed) {
                        snprintf(str, sizeof(str), "stChnAttr.stRcAttr.stH265Avbr.u32MaxBitRate = %d;\n", max_br_value);
                        fputs(str, fp);
                    }

                    snprintf(str, sizeof(str), "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h264") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264AVBR;
                if (br_changed) {
                    stChnAttr.stRcAttr.stH264Avbr.u32BitRate = br_value;
                }
                if (min_br_changed) {
                    stChnAttr.stRcAttr.stH264Avbr.u32MinBitRate = min_br_value;
                }
                if (max_br_changed) {
                    stChnAttr.stRcAttr.stH264Avbr.u32MaxBitRate = max_br_value;
                }
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);
            }
        } else if (rc_mode_changed && strcasecmp(rc_mode_value, "fixqp") == 0) {
            if (type && strcasecmp(type, "h265") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265FIXQP;
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265FIXQP;\n"
                             "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id, venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h264") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;\n"
                             "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id, venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "mjpeg") == 0) {
                VENC_CHN_ATTR_S stChnAttr;
                RK_MPI_VENC_GetChnAttr(venc_id, &stChnAttr);
                stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
                RK_MPI_VENC_SetChnAttr(venc_id, &stChnAttr);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str), 
                             "VENC_CHN_ATTR_S stChnAttr;\n"
                             "RK_MPI_VENC_GetChnAttr(%d, &stChnAttr);\n"
                             "stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;\n"
                             "RK_MPI_VENC_SetChnAttr(%d, &stChnAttr);\n", venc_id, venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            }
        }
    }
}

void check_and_set_cu_dqp_params(int venc_id, const char* type, const cJSON* venc) {
    int cu_dqp_changed = 0;
    int cu_dqp_value = -1;

    cJSON* cu_dqp = cJSON_GetObjectItem(venc, "CuDqp");
    if (cu_dqp) {
        if (cJSON_IsNumber(cu_dqp)) {
            cu_dqp_value = cu_dqp->valueint;
            cu_dqp_changed = 1;
        }
    }

    if (cu_dqp_changed) {
        VENC_H265_CU_DQP_S stCuDqp;
        RK_MPI_VENC_GetH265CuDqp(venc_id, &stCuDqp);
        stCuDqp.u32CuDqp = cu_dqp_value;
        RK_MPI_VENC_SetH265CuDqp(venc_id, &stCuDqp);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_H265_CU_DQP_S stCuDqp;\n"
                     "RK_MPI_VENC_GetH265CuDqp(%d, &stCuDqp);\n"
                     "stCuDqp.u32CuDqp = %d;\n"
                     "RK_MPI_VENC_SetH265CuDqp(%d, &stCuDqp);\n",
                     venc_id, cu_dqp_value, venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_debreath_strength_params(int venc_id, const char* type, const cJSON* venc) {
    int strength0_changed = 0;
    int strength0_value = -1;

    cJSON* strength0 = cJSON_GetObjectItem(venc, "Strength0");
    if (strength0) {
        if (cJSON_IsNumber(strength0)) {
            strength0_value = strength0->valueint;
            strength0_changed = 1;
        }
    }

    if (strength0_changed) {
        VENC_DEBREATHEFFECT_S stDeBreathEffect;
        RK_MPI_VENC_GetDeBreathEffect(venc_id, &stDeBreathEffect);
        stDeBreathEffect.bEnable = strength0_value > 0 ? RK_TRUE : RK_FALSE;
        stDeBreathEffect.s32Strength0 = strength0_value;
        stDeBreathEffect.s32Strength1 = strength0_value;
        RK_MPI_VENC_SetDeBreathEffect(venc_id, &stDeBreathEffect);

        OPEN_VENC_SET;
        if (fp) {
            char str[1024];
            fputs("{\n", fp);
            snprintf(str, sizeof(str), 
                     "VENC_DEBREATHEFFECT_S stDeBreathEffect;\n"
                     "RK_MPI_VENC_GetDeBreathEffect(%d, &stDeBreathEffect);\n"
                     "stDeBreathEffect.bEnable = %s;\n"
                     "stDeBreathEffect.s32Strength0 = %d;\n"
                     "stDeBreathEffect.s32Strength1 = %d;\n"
                     "RK_MPI_VENC_SetDeBreathEffect(%d, &stDeBreathEffect);\n",
                     venc_id, strength0_value > 0 ? "RK_TRUE" : "RK_FALSE",
                     strength0_value, strength0_value, venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}

void check_and_set_color_range_params(int venc_id, const char* type, const cJSON* venc) {
    int color_range_changed = 0;
    char* color_range_value = NULL;
    int flag = -1;

    cJSON* color_range = cJSON_GetObjectItem(venc, "ColorRange");
    if (color_range) {
        if (cJSON_IsString(color_range)) {
            color_range_value = color_range->valuestring;
            color_range_changed = 1;
        }
    }

    if (color_range_changed) {
        if (strcasecmp(color_range_value, "limit") == 0) {
            flag = 0;
        } else if (strcasecmp(color_range_value, "full") == 0) {
            flag = 1;
        }

        if (flag != -1) {
            if (type && strcasecmp(type, "h264") == 0) {
                VENC_H264_VUI_S stH264Vui;
                RK_MPI_VENC_GetH264Vui(venc_id, &stH264Vui);
                stH264Vui.stVuiVideoSignal.video_full_range_flag = flag;
                RK_MPI_VENC_SetH264Vui(venc_id, &stH264Vui);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str),
                             "VENC_H264_VUI_S stH264Vui;\n"
                             "RK_MPI_VENC_GetH264Vui(%d, &stH264Vui);\n"
                             "stH264Vui.stVuiVideoSignal.video_full_range_flag = %d;\n"
                             "RK_MPI_VENC_SetH264Vui(%d, &stH264Vui);\n",
                             venc_id, flag, venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            } else if (type && strcasecmp(type, "h265") == 0) {
                VENC_H265_VUI_S stH265Vui;
                RK_MPI_VENC_GetH265Vui(venc_id, &stH265Vui);
                stH265Vui.stVuiVideoSignal.video_full_range_flag = flag;
                RK_MPI_VENC_SetH265Vui(venc_id, &stH265Vui);

                OPEN_VENC_SET;
                if (fp) {
                    char str[1024];
                    fputs("{\n", fp);
                    snprintf(str, sizeof(str),
                             "VENC_H265_VUI_S stH265Vui;\n"
                             "RK_MPI_VENC_GetH265Vui(%d, &stH265Vui);\n"
                             "stH265Vui.stVuiVideoSignal.video_full_range_flag = %d;\n"
                             "RK_MPI_VENC_SetH265Vui(%d, &stH265Vui);\n",
                             venc_id, flag, venc_id);
                    fputs(str, fp);
                    fputs("}\n\n", fp);
                }
                CLOSE_VENC_SET;
            }
        }
    }
}

#ifdef SMART_V3
void check_and_set_svc3_qp_params(int venc_id, const char* type, const cJSON* venc) {
    int bmap_qpmin_i_changed = 0;
    int bmap_qpmax_i_changed = 0;
    int bmap_qpmin_p_changed = 0;
    int bmap_qpmax_p_changed = 0;
    int min_bg_fqp_changed = 0;
    int max_bg_fqp_changed = 0;
    int min_fg_fqp_changed = 0;
    int max_fg_fqp_changed = 0;
    int bg_delta_qp_i_changed = 0;
    int bg_delta_qp_p_changed = 0;
    int fg_delta_qp_i_changed = 0;
    int fg_delta_qp_p_changed = 0;
    int fg_area_changed = 0;

    int bmap_qpmin_i_value = -1;
    int bmap_qpmax_i_value = -1;
    int bmap_qpmin_p_value = -1;
    int bmap_qpmax_p_value = -1;
    int min_bg_fqp_value = -1;
    int max_bg_fqp_value = -1;
    int min_fg_fqp_value = -1;
    int max_fg_fqp_value = -1;
    int bg_delta_qp_i_value = 0;
    int bg_delta_qp_p_value = 0;
    int fg_delta_qp_i_value = 0;
    int fg_delta_qp_p_value = 0;
    int fg_area_value = -1;

    cJSON* bmap_qpmin_i = cJSON_GetObjectItem(venc, "bmap_qpmin_i");
    if (bmap_qpmin_i) {
        if (cJSON_IsNumber(bmap_qpmin_i)) {
            bmap_qpmin_i_value = bmap_qpmin_i->valueint;
            bmap_qpmin_i_changed = 1;
        }
    }

    cJSON* bmap_qpmax_i = cJSON_GetObjectItem(venc, "bmap_qpmax_i");
    if (bmap_qpmax_i) {
        if (cJSON_IsNumber(bmap_qpmax_i)) {
            bmap_qpmax_i_value = bmap_qpmax_i->valueint;
            bmap_qpmax_i_changed = 1;
        }
    }

    cJSON* bmap_qpmin_p = cJSON_GetObjectItem(venc, "bmap_qpmin_p");
    if (bmap_qpmin_p) {
        if (cJSON_IsNumber(bmap_qpmin_p)) {
            bmap_qpmin_p_value = bmap_qpmin_p->valueint;
            bmap_qpmin_p_changed = 1;
        }
    }

    cJSON* bmap_qpmax_p = cJSON_GetObjectItem(venc, "bmap_qpmax_p");
    if (bmap_qpmax_p) {
        if (cJSON_IsNumber(bmap_qpmax_p)) {
            bmap_qpmax_p_value = bmap_qpmax_p->valueint;
            bmap_qpmax_p_changed = 1;
        }
    }

    cJSON* min_bg_fqp = cJSON_GetObjectItem(venc, "min_bg_fqp");
    if (min_bg_fqp) {
        if (cJSON_IsNumber(min_bg_fqp)) {
            min_bg_fqp_value = min_bg_fqp->valueint;
            min_bg_fqp_changed = 1;
        }
    }

    cJSON* max_bg_fqp = cJSON_GetObjectItem(venc, "max_bg_fqp");
    if (max_bg_fqp) {
        if (cJSON_IsNumber(max_bg_fqp)) {
            max_bg_fqp_value = max_bg_fqp->valueint;
            max_bg_fqp_changed = 1;
        }
    }

    cJSON* min_fg_fqp = cJSON_GetObjectItem(venc, "min_fg_fqp");
    if (min_fg_fqp) {
        if (cJSON_IsNumber(min_fg_fqp)) {
            min_fg_fqp_value = min_fg_fqp->valueint;
            min_fg_fqp_changed = 1;
        }
    }

    cJSON* max_fg_fqp = cJSON_GetObjectItem(venc, "max_fg_fqp");
    if (max_fg_fqp) {
        if (cJSON_IsNumber(max_fg_fqp)) {
            max_fg_fqp_value = max_fg_fqp->valueint;
            max_fg_fqp_changed = 1;
        }
    }

    cJSON* bg_delta_qp_i = cJSON_GetObjectItem(venc, "bg_delta_qp_i");
    if (bg_delta_qp_i) {
        if (cJSON_IsNumber(bg_delta_qp_i)) {
            bg_delta_qp_i_value = bg_delta_qp_i->valueint;
            bg_delta_qp_i_changed = 1;
        }
    }

    cJSON* bg_delta_qp_p = cJSON_GetObjectItem(venc, "bg_delta_qp_p");
    if (bg_delta_qp_p) {
        if (cJSON_IsNumber(bg_delta_qp_p)) {
            bg_delta_qp_p_value = bg_delta_qp_p->valueint;
            bg_delta_qp_p_changed = 1;
        }
    }

    cJSON* fg_delta_qp_i = cJSON_GetObjectItem(venc, "fg_delta_qp_i");
    if (fg_delta_qp_i) {
        if (cJSON_IsNumber(fg_delta_qp_i)) {
            fg_delta_qp_i_value = fg_delta_qp_i->valueint;
            fg_delta_qp_i_changed = 1;
        }
    }

    cJSON* fg_delta_qp_p = cJSON_GetObjectItem(venc, "fg_delta_qp_p");
    if (fg_delta_qp_p) {
        if (cJSON_IsNumber(fg_delta_qp_p)) {
            fg_delta_qp_p_value = fg_delta_qp_p->valueint;
            fg_delta_qp_p_changed = 1;
        }
    }

    cJSON* fg_area = cJSON_GetObjectItem(venc, "fg_area");
    if (fg_area) {
        if (cJSON_IsNumber(fg_area)) {
            fg_area_value = fg_area->valueint;
            fg_area_changed = 1;
        }
    }

    if (bmap_qpmin_i_changed || bmap_qpmax_i_changed || bmap_qpmin_p_changed || bmap_qpmax_p_changed ||
        min_bg_fqp_changed || max_bg_fqp_changed || min_fg_fqp_changed || max_fg_fqp_changed ||
        bg_delta_qp_i_changed || bg_delta_qp_p_changed || fg_delta_qp_i_changed || fg_delta_qp_p_changed ||
        fg_area_changed) {
        VENC_SVC3_PARAM_S stSvc3Param;
        memset(&stSvc3Param, 0, sizeof(stSvc3Param));
        stSvc3Param.u32BgFrmMinQp = min_bg_fqp_value;
        stSvc3Param.u32BgFrmMaxQp = max_bg_fqp_value;
        stSvc3Param.u32FgFrmMinQp = min_fg_fqp_value;
        stSvc3Param.u32FgFrmMaxQp = max_fg_fqp_value;
        stSvc3Param.u32BmapMinIQp = bmap_qpmin_i_value;
        stSvc3Param.u32BmapMaxIQp = bmap_qpmax_i_value;
        stSvc3Param.u32BmapMinPQp = bmap_qpmin_p_value;
        stSvc3Param.u32BmapMaxPQp = bmap_qpmax_p_value;
        stSvc3Param.s32BgDeltaIQp = bg_delta_qp_i_value;
        stSvc3Param.s32BgDeltaPQp = bg_delta_qp_p_value;
        stSvc3Param.s32FgDeltaIQp = fg_delta_qp_i_value;
        stSvc3Param.s32FgDeltaPQp = fg_delta_qp_p_value;
        stSvc3Param.s32FgArea = fg_area_value;
        RK_MPI_VENC_SetSvc3Param(venc_id, &stSvc3Param);

        OPEN_VENC_SET;
        if (fp) {
            char str[2048];
            fputs("{\n", fp);
            snprintf(str, sizeof(str),
                     "VENC_SVC3_PARAM_S stSvc3Param;\n"
                     "memset(&stSvc3Param, 0, sizeof(stSvc3Param));\n");
            fputs(str, fp);
            if (min_bg_fqp_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32BgFrmMinQp = %d;\n", min_bg_fqp_value);
                fputs(str, fp);
            }
            if (max_bg_fqp_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32BgFrmMaxQp = %d;\n", max_bg_fqp_value);
                fputs(str, fp);
            }
            if (min_fg_fqp_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32FgFrmMinQp = %d;\n", min_fg_fqp_value);
                fputs(str, fp);
            }
            if (max_fg_fqp_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32FgFrmMaxQp = %d;\n", max_fg_fqp_value);
                fputs(str, fp);
            }
            if (bmap_qpmin_i_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32BmapMinIQp = %d;\n", bmap_qpmin_i_value);
                fputs(str, fp);
            }
            if (bmap_qpmax_i_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32BmapMaxIQp = %d;\n", bmap_qpmax_i_value);
                fputs(str, fp);
            }
            if (bmap_qpmin_p_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32BmapMinPQp = %d;\n", bmap_qpmin_p_value);
                fputs(str, fp);
            }
            if (bmap_qpmax_p_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.u32BmapMaxPQp = %d;\n", bmap_qpmax_p_value);
                fputs(str, fp);
            }
            if (bg_delta_qp_i_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.s32BgDeltaIQp = %d;\n", bg_delta_qp_i_value);
                fputs(str, fp);
            }
            if (bg_delta_qp_p_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.s32BgDeltaPQp = %d;\n", bg_delta_qp_p_value);
                fputs(str, fp);
            }
            if (fg_delta_qp_i_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.s32FgDeltaIQp = %d;\n", fg_delta_qp_i_value);
                fputs(str, fp);
            }
            if (fg_delta_qp_p_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.s32FgDeltaPQp = %d;\n", fg_delta_qp_p_value);
                fputs(str, fp);
            }
            if (fg_area_changed) {
                snprintf(str, sizeof(str), "stSvc3Param.s32FgArea = %d;\n", fg_area_value);
                fputs(str, fp);
            }
            snprintf(str, sizeof(str), "RK_MPI_VENC_SetSvc3Param(%d, &stSvc3Param);\n", venc_id);
            fputs(str, fp);
            fputs("}\n\n", fp);
        }
        CLOSE_VENC_SET;
    }
}
#endif

static int print_server_ip(void) {
    int sockfd;
    struct ifreq ifr;
    char ip_address[INET_ADDRSTRLEN];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);

    if (ioctl(sockfd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl");
        close(sockfd);
        return -1;
    }

    inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr,
              ip_address, INET_ADDRSTRLEN);

    printf("server: %s\n", ip_address);

    close(sockfd);
    return 0;
}

int main() {
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int new_socket;
    char buffer[BUFFER_SIZE] = {0};
    RK_S32 s32Ret;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    s32Ret = RK_MPI_SYS_Init();
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        exit(EXIT_FAILURE);
    }

    print_server_ip();

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(address.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("client: %s\n", client_ip);

        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = read(new_socket, buffer, BUFFER_SIZE);

        if (bytes_read > 0) {

            buffer[bytes_read] = '\0';

            if (strcasecmp(buffer, "GET_VENC_INFO") == 0) {
                char* venc_info = get_venc_info();
                if (venc_info) {
                    write(new_socket, venc_info, strlen(venc_info));
                    free(venc_info);
                } else {
                    const char* error_msg = "获取venc_info失败";
                    write(new_socket, error_msg, strlen(error_msg));
                }
            } else if (strcasecmp(buffer, "GET_VENC_SET") == 0) {

                char* venc_set = get_venc_set();
                if (venc_set) {
                    write(new_socket, venc_set, strlen(venc_set));
                    free(venc_set);
                } else {
                    const char* error_msg = "获取venc_set失败";
                    write(new_socket, error_msg, strlen(error_msg));
                }
            } else {
                char* formatted_json = format_json_with_cjson(buffer);
                if (formatted_json) {
                    printf("\nsetting:\n%s\n", formatted_json);

                    free(formatted_json);
                }
            }
        }

        close(new_socket);
    }

    close(server_fd);

    RK_MPI_SYS_Exit();

    return 0;
}
