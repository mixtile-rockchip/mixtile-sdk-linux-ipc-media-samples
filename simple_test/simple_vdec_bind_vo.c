#include "rk_type.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vdec.h"
#include "rk_comm_vdec.h"
#include "rk_comm_vo.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_mmz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>

#ifdef RV1126B
#define DEFAULT_VO_DEV 0
#define DEFAULT_VO_LAYER 0
#define DEFAULT_VO_INTFTYPE VO_INTF_MIPI
#define VO_LAYER_W 1920
#define VO_LAYER_H 1080
#define VO_LAYER_RECT_W 640
#define VO_LAYER_RECT_H 480
#elif defined(RV1106_RV1103)
#define DEFAULT_VO_DEV 0
#define DEFAULT_VO_LAYER 0
#define DEFAULT_VO_INTFTYPE VO_INTF_DEFAULT
#define VO_LAYER_W 320
#define VO_LAYER_H 480
#define VO_LAYER_RECT_W 320
#define VO_LAYER_RECT_H 480
#elif defined(RK3588)
#define DEFAULT_VO_DEV 3
#define DEFAULT_VO_LAYER 7
#define DEFAULT_VO_INTFTYPE VO_INTF_MIPI
#define VO_LAYER_W 1920
#define VO_LAYER_H 1080
#define VO_LAYER_RECT_W 640
#define VO_LAYER_RECT_H 480
#else
#define DEFAULT_VO_DEV 1
#define DEFAULT_VO_LAYER 5
#define DEFAULT_VO_INTFTYPE VO_INTF_MIPI
#define VO_LAYER_W 1920
#define VO_LAYER_H 1080
#define VO_LAYER_RECT_W 640
#define VO_LAYER_RECT_H 480
#endif
#define DEFAULT_VDEC_CHN 0
#define DEFAULT_VO_CHN 0
#define DEFAULT_H264_FILE_PATH "test.h264"
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define BUFFER_SIZE (1024 * 1024)

// 检查函数返回值，若失败则打印错误信息并退出程序
static void check_ret(RK_S32 ret, const char* func_name) {
    if (ret != RK_SUCCESS) {
        fprintf(stderr, "[%s] failed with error code: 0x%x\n", func_name, ret);
        RK_MPI_SYS_Exit();
        exit(EXIT_FAILURE);
    }
}

// 查找下一个 NAL 单元的起始位置
static int find_next_nal_unit(const unsigned char *buf, int size) {
    for (int i = 0; i < size - 3; i++) {
        if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x00 && buf[i + 3] == 0x01) {
            return i;
        }
        if (i < size - 2 && buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
            return i;
        }
    }
    return -1;
}

// 判断是否为 NAL 单元起始
int is_nal_start(const unsigned char* buf) {
    return (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x01) ||
           (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01);
}

// 线程函数，用于发送 H.264 数据
void* send_h264_data(void* arg) {
    struct {
        int vdec_chn;
        const char* file_path;
    } *data = (struct { int vdec_chn; const char* file_path; } *)arg;

    FILE* h264_file = fopen(data->file_path, "rb");
    if (h264_file == NULL) {
        perror("Failed to open H264 file");
        return NULL;
    }

    VDEC_STREAM_S vdec_stream;
    MB_BLK mb_blk;
    RK_U8* buffer = (RK_U8*)malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        perror("Failed to allocate buffer");
        fclose(h264_file);
        return NULL;
    }

    RK_U32 read_size;
    int nal_start = 0;
    int nal_end;
    int last_nal_start = 0;  // 新增变量，用于保存上一次有效的 nal_start 位置

    while ((read_size = fread(buffer + nal_start, 1, BUFFER_SIZE - nal_start, h264_file)) > 0) {
        read_size += nal_start;
        nal_start = 0;
        last_nal_start = 0;

        while (1) {
            nal_end = find_next_nal_unit(buffer + nal_start, read_size - nal_start);
            if (nal_end == -1) {
                // 如果没有找到下一个 NAL 单元，将剩余数据保留到下一次读取
                if (nal_start < read_size) {
                    memmove(buffer, buffer + nal_start, read_size - nal_start);
                    nal_start = read_size - nal_start;
                    last_nal_start = nal_start;
                }
                break;
            }
            nal_end += nal_start;
            if (nal_end > nal_start) {
                RK_S32 ret = RK_MPI_MMZ_Alloc(&mb_blk, nal_end - last_nal_start, 0);
                check_ret(ret, "RK_MPI_SYS_CreateMB");
                memcpy(RK_MPI_MB_Handle2VirAddr(mb_blk), buffer + last_nal_start, nal_end - last_nal_start);
                vdec_stream.pMbBlk = mb_blk;
                vdec_stream.u32Len = nal_end - last_nal_start;
                vdec_stream.u64PTS = 0;
                vdec_stream.bEndOfStream = 0;
                vdec_stream.bBypassMbBlk = RK_TRUE;
                ret = RK_MPI_VDEC_SendStream(data->vdec_chn, &vdec_stream, 1000);
                check_ret(ret, "RK_MPI_VDEC_SendStream");

                RK_MPI_MMZ_Free(mb_blk);
               // 更新 last_nal_start 和 nal_start
                last_nal_start = nal_end;
                nal_start = nal_end;
            } else {
                // 若未找到有效 NAL 单元，手动移动 nal_start
                if (nal_start == 0) {
                    last_nal_start = 0;
                }
                nal_start++;
            }
        }
    }

    // 处理最后一个 NAL 单元
    if (nal_start < read_size) {
        RK_S32 ret = RK_MPI_MMZ_Alloc(&mb_blk, read_size - nal_start, 1000);
        check_ret(ret, "RK_MPI_SYS_CreateMB");

        memcpy(RK_MPI_MB_Handle2VirAddr(mb_blk), buffer + nal_start, read_size - nal_start);

        vdec_stream.pMbBlk = mb_blk;
        vdec_stream.u32Len = read_size - nal_start;
        vdec_stream.u64PTS = 0;
        vdec_stream.bEndOfStream = 1; // 最后一个流，设置结束标志
        vdec_stream.bBypassMbBlk = RK_TRUE;

        ret = RK_MPI_VDEC_SendStream(data->vdec_chn, &vdec_stream, 1000);
        check_ret(ret, "RK_MPI_VDEC_SendStream");

        RK_MPI_MMZ_Free(mb_blk);
    }

    fclose(h264_file);
    free(buffer);

    return NULL;
}

void* vdec_get_send_vo(void* arg) {
    VIDEO_FRAME_INFO_S sFrame;
    struct {
        MPP_CHN_S src_chn;
        MPP_CHN_S dest_chn;
        int loop;
    } *data = (struct { MPP_CHN_S src_chn; MPP_CHN_S dest_chn; int loop;} *)arg;
    int ret;

    while (1) {
        ret = RK_MPI_VDEC_GetFrame(data->src_chn.s32ChnId, &sFrame, 1000);
        if (ret == RK_SUCCESS)
            RK_MPI_VO_SendFrame(data->dest_chn.s32DevId, data->dest_chn.s32ChnId, &sFrame, -1);
        else if (data->loop == 0)
            break;
        ret = RK_MPI_VDEC_ReleaseFrame(data->src_chn.s32ChnId, &sFrame);
    }
}

int main(int argc, char* argv[]) {
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    const char* h264_file_path = DEFAULT_H264_FILE_PATH;
    int bind = 1;

    // 定义长选项
    static struct option long_options[] = {
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"input", required_argument, 0, 'i'},
        {"bind", required_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "w:h:i:b:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'w':
                width = atoi(optarg);
                break;
            case 'h':
                height = atoi(optarg);
                break;
            case 'i':
                h264_file_path = optarg;
                break;
            case 'b':
                bind = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-w|--width <width>] [-h|--height <height>] [-i|--input <h264_file_path>]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }
    printf("param list: w = %d, h = %d, i = %s, b = %d\n", width, height, h264_file_path, bind);

    // 初始化 RK MPI 系统
    RK_S32 ret = RK_MPI_SYS_Init();
    check_ret(ret, "RK_MPI_SYS_Init");

    // 初始化 VDEC 模块参数
    VDEC_MOD_PARAM_S vdec_mod_param;
    memset(&vdec_mod_param, 0, sizeof(VDEC_MOD_PARAM_S));
    vdec_mod_param.enVdecMBSource = MB_SOURCE_MODULE;
    ret = RK_MPI_VDEC_SetModParam(&vdec_mod_param);
    check_ret(ret, "RK_MPI_VDEC_SetModParam");

    // 创建 VDEC 通道，使用帧模式
    VDEC_CHN_ATTR_S vdec_chn_attr;
    memset(&vdec_chn_attr, 0, sizeof(VDEC_CHN_ATTR_S));
    vdec_chn_attr.enType = RK_VIDEO_ID_AVC;
    vdec_chn_attr.enMode = VIDEO_MODE_FRAME; // 使用帧模式
    vdec_chn_attr.u32PicWidth = width;
    vdec_chn_attr.u32PicHeight = height;
    vdec_chn_attr.u32FrameBufCnt = 6;
    vdec_chn_attr.u32StreamBufCnt = 8;
    vdec_chn_attr.u32FrameBufDepth = 1;
    ret = RK_MPI_VDEC_CreateChn(DEFAULT_VDEC_CHN, &vdec_chn_attr);
    check_ret(ret, "RK_MPI_VDEC_CreateChn");

    // 启动 VDEC 接收码流
    ret = RK_MPI_VDEC_StartRecvStream(DEFAULT_VDEC_CHN);
    check_ret(ret, "RK_MPI_VDEC_StartRecvStream");

    // 创建线程发送 H264 数据
    pthread_t h264_thread;
    struct {
        int vdec_chn;
        const char* file_path;
    } thread_data = {DEFAULT_VDEC_CHN, h264_file_path};
    ret = pthread_create(&h264_thread, NULL, send_h264_data, &thread_data);
    if (ret != 0) {
        perror("Failed to create thread");
        RK_MPI_VDEC_StopRecvStream(DEFAULT_VDEC_CHN);
        RK_MPI_VDEC_DestroyChn(DEFAULT_VDEC_CHN);
        RK_MPI_SYS_Exit();
        return EXIT_FAILURE;
    }

     // 绑定图层到设备
    ret = RK_MPI_VO_BindLayer(DEFAULT_VO_LAYER, DEFAULT_VO_DEV, VO_LAYER_MODE_VIDEO);
    if (ret != RK_SUCCESS) {
        printf("RK_MPI_VO_BindLayer failed with error code: %x\n", ret);
        return ret;
    }
    printf("Layer %d is successfully bound to device %d.\n", DEFAULT_VO_LAYER, DEFAULT_VO_DEV);

    // 配置 VO 设备
    VO_PUB_ATTR_S vo_pub_attr;
    memset(&vo_pub_attr, 0, sizeof(VO_PUB_ATTR_S));
    vo_pub_attr.enIntfType = DEFAULT_VO_INTFTYPE;
    vo_pub_attr.enIntfSync = VO_OUTPUT_DEFAULT;
    ret = RK_MPI_VO_SetPubAttr(DEFAULT_VO_DEV, &vo_pub_attr);
    check_ret(ret, "RK_MPI_VO_SetPubAttr");

    RK_MPI_VO_SetLayerSpliceMode(DEFAULT_VO_LAYER, VO_SPLICE_MODE_RGA);

    ret = RK_MPI_VO_Enable(DEFAULT_VO_DEV);
    check_ret(ret, "RK_MPI_VO_Enable");

    // 配置 VO 图层属性
    VO_VIDEO_LAYER_ATTR_S vo_layer_attr;
    memset(&vo_layer_attr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
    vo_layer_attr.enPixFormat = RK_FMT_RGB888;
    vo_layer_attr.stDispRect.s32X = 0;
    vo_layer_attr.stDispRect.s32Y = 0;
    vo_layer_attr.stDispRect.u32Width = VO_LAYER_RECT_W;
    vo_layer_attr.stDispRect.u32Height = VO_LAYER_RECT_H;
    vo_layer_attr.stImageSize.u32Width = VO_LAYER_W;
    vo_layer_attr.stImageSize.u32Height = VO_LAYER_H;
    ret = RK_MPI_VO_SetLayerAttr(DEFAULT_VO_LAYER, &vo_layer_attr);
    check_ret(ret, "RK_MPI_VO_SetLayerAttr");

    ret = RK_MPI_VO_EnableLayer(DEFAULT_VO_LAYER);
    check_ret(ret, "RK_MPI_VO_EnableLayer");

    // 配置 VO 通道属性
    VO_CHN_ATTR_S vo_chn_attr;
    memset(&vo_chn_attr, 0, sizeof(VO_CHN_ATTR_S));
    vo_chn_attr.stRect.s32X = 0;
    vo_chn_attr.stRect.s32Y = 0;
    vo_chn_attr.stRect.u32Width = VO_LAYER_W;
    vo_chn_attr.stRect.u32Height = VO_LAYER_H;
    ret = RK_MPI_VO_SetChnAttr(DEFAULT_VO_LAYER, DEFAULT_VO_CHN, &vo_chn_attr);
    check_ret(ret, "RK_MPI_VO_SetChnAttr");

    ret = RK_MPI_VO_EnableChn(DEFAULT_VO_LAYER, DEFAULT_VO_CHN);
    check_ret(ret, "RK_MPI_VO_EnableChn");

    pthread_t vo_thread;
    struct {
        MPP_CHN_S src_chn;
        MPP_CHN_S dest_chn;
        int loop;
    } vo_thread_data;
    // 绑定 VO 图层到 VO 设备
    MPP_CHN_S src_chn = {.enModId = RK_ID_VDEC, .s32DevId = 0, .s32ChnId = DEFAULT_VDEC_CHN};
    MPP_CHN_S dest_chn = {.enModId = RK_ID_VO, .s32DevId = DEFAULT_VO_LAYER, .s32ChnId = DEFAULT_VO_CHN}; // 设备层一般通道号为 0

    if (bind) {
        ret = RK_MPI_SYS_Bind(&src_chn, &dest_chn);
        check_ret(ret, "RK_MPI_SYS_Bind");
        RK_LOGI("src_chn[%d, %d, %d] bind dest_chn[%d, %d, %d]", src_chn.enModId, src_chn.s32DevId, src_chn.s32ChnId, dest_chn.enModId, dest_chn.s32DevId, dest_chn.s32ChnId);
    } else {
        vo_thread_data.src_chn = src_chn;
        vo_thread_data.dest_chn = dest_chn;
        vo_thread_data.loop = 1;
        pthread_create(&vo_thread, NULL, vdec_get_send_vo, &vo_thread_data);
    }
    // 等待发送 H264 数据的线程结束
    ret = pthread_join(h264_thread, NULL);
    if (ret != 0) {
        perror("Failed to join h264_thread thread");
    }

    if (bind) {
        // 解除绑定
        ret = RK_MPI_SYS_UnBind(&src_chn, &dest_chn);
        check_ret(ret, "RK_MPI_SYS_UnBind");
    } else {
        vo_thread_data.loop = 0;
        ret = pthread_join(vo_thread, NULL);
        if (ret != 0) {
            perror("Failed to join vo_thread thread");
        }
    }

    // 释放资源
    ret = RK_MPI_VO_DisableChn(DEFAULT_VO_LAYER, DEFAULT_VO_CHN);
    check_ret(ret, "RK_MPI_VO_DisableChn");

    ret = RK_MPI_VO_DisableLayer(DEFAULT_VO_LAYER);
    check_ret(ret, "RK_MPI_VO_DisableLayer");

    ret = RK_MPI_VO_Disable(DEFAULT_VO_DEV);
    check_ret(ret, "RK_MPI_VO_Disable");

    ret = RK_MPI_VO_UnBindLayer(DEFAULT_VO_LAYER, DEFAULT_VO_DEV);
    check_ret(ret, "RK_MPI_VO_UnBindLayer");

    ret = RK_MPI_VDEC_StopRecvStream(DEFAULT_VDEC_CHN);
    check_ret(ret, "RK_MPI_VDEC_StopRecvStream");

    ret = RK_MPI_VDEC_DestroyChn(DEFAULT_VDEC_CHN);
    check_ret(ret, "RK_MPI_VDEC_DestroyChn");

    ret = RK_MPI_SYS_Exit();
    check_ret(ret, "RK_MPI_SYS_Exit");

    return 0;
}