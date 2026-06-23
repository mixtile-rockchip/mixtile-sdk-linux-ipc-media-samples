#include "rk_type.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vdec.h"
#include "rk_comm_vdec.h"
#include "rk_comm_vo.h"
#include "rk_comm_vpss.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_mmz.h"
#include "rk_mpi_vpss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>

#ifdef RV1126B
#define DEFAULT_VO_DEV 0
#define DEFAULT_VO_LAYER 0
#elif defined(RK3588)
#define DEFAULT_VO_DEV 3
#define DEFAULT_VO_LAYER 7
#else
#define DEFAULT_VO_DEV 1
#define DEFAULT_VO_LAYER 5
#endif

#define DEFAULT_VDEC_CHN 0
#define DEFAULT_VPSS_GRP 0
#define DEFAULT_VPSS_CHN 0
#define DEFAULT_VO_CHN 0
#define DEFAULT_H264_FILE_PATH "test.h264"
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define BUFFER_SIZE (1024 * 1024)

// 线程参数：共享给生产者与线程函数，避免匿名结构体类型不兼容
struct h264_send_arg {
	int vdec_chn;
	const char *file_path;
};

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
    struct h264_send_arg *data = (struct h264_send_arg *)arg;

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
        RK_S32 ret = RK_MPI_MMZ_Alloc(&mb_blk, read_size - nal_start, 0);
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

int main(int argc, char* argv[]) {
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    const char* h264_file_path = DEFAULT_H264_FILE_PATH;

    // 定义长选项
    static struct option long_options[] = {
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"input", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "w:h:i:", long_options, NULL)) != -1) {
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
            default:
                fprintf(stderr, "Usage: %s [-w|--width <width>] [-h|--height <height>] [-i|--input <h264_file_path>]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

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
    vdec_chn_attr.enMode = VIDEO_MODE_FRAME;
    vdec_chn_attr.u32PicWidth = width;
    vdec_chn_attr.u32PicHeight = height;
    vdec_chn_attr.u32FrameBufCnt = 6;
    vdec_chn_attr.u32StreamBufCnt = 8;
    ret = RK_MPI_VDEC_CreateChn(DEFAULT_VDEC_CHN, &vdec_chn_attr);
    check_ret(ret, "RK_MPI_VDEC_CreateChn");

    // 启动 VDEC 接收码流
    ret = RK_MPI_VDEC_StartRecvStream(DEFAULT_VDEC_CHN);
    check_ret(ret, "RK_MPI_VDEC_StartRecvStream");

    // 创建 VPSS GROUP
    VPSS_GRP_ATTR_S vpss_grp_attr;
    memset(&vpss_grp_attr, 0, sizeof(VPSS_GRP_ATTR_S));
    vpss_grp_attr.u32MaxW = width;
    vpss_grp_attr.u32MaxH = height;
    vpss_grp_attr.enPixelFormat = RK_FMT_YUV420SP;
#ifdef RV1126B
    vpss_grp_attr.u32MaxQueue = 10;
#endif
    ret = RK_MPI_VPSS_CreateGrp(DEFAULT_VPSS_GRP, &vpss_grp_attr);
    check_ret(ret, "RK_MPI_VPSS_CreateGrp");

    // 设置 VPSS 通道属性
    VPSS_CHN_ATTR_S vpss_chn_attr;
    memset(&vpss_chn_attr, 0, sizeof(VPSS_CHN_ATTR_S));
    vpss_chn_attr.enChnMode = VPSS_CHN_MODE_USER;
    vpss_chn_attr.u32Width = width;
    vpss_chn_attr.u32Height = height;
    vpss_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vpss_chn_attr.u32FrameBufCnt = 3;
    ret = RK_MPI_VPSS_SetChnAttr(DEFAULT_VPSS_GRP, DEFAULT_VPSS_CHN, &vpss_chn_attr);
    check_ret(ret, "RK_MPI_VPSS_SetChnAttr");

    // 启用 VPSS 通道
    ret = RK_MPI_VPSS_EnableChn(DEFAULT_VPSS_GRP, DEFAULT_VPSS_CHN);
    check_ret(ret, "RK_MPI_VPSS_EnableChn");

    // 启动 VPSS GROUP
    ret = RK_MPI_VPSS_StartGrp(DEFAULT_VPSS_GRP);
    check_ret(ret, "RK_MPI_VPSS_StartGrp");

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
    vo_pub_attr.enIntfType = VO_INTF_MIPI;
    vo_pub_attr.enIntfSync = VO_OUTPUT_DEFAULT;
    ret = RK_MPI_VO_SetPubAttr(DEFAULT_VO_DEV, &vo_pub_attr);
    check_ret(ret, "RK_MPI_VO_SetPubAttr");

    ret = RK_MPI_VO_Enable(DEFAULT_VO_DEV);
    check_ret(ret, "RK_MPI_VO_Enable");

    // 配置 VO 图层属性
    VO_VIDEO_LAYER_ATTR_S vo_layer_attr;
    memset(&vo_layer_attr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
    vo_layer_attr.enPixFormat = RK_FMT_YUV420SP;
    vo_layer_attr.stDispRect.s32X = 0;
    vo_layer_attr.stDispRect.s32Y = 480;
    vo_layer_attr.stDispRect.u32Width = 640;
    vo_layer_attr.stDispRect.u32Height = 480;
    vo_layer_attr.stImageSize.u32Width = 1920;
    vo_layer_attr.stImageSize.u32Height = 1080;
    ret = RK_MPI_VO_SetLayerAttr(DEFAULT_VO_LAYER, &vo_layer_attr);
    check_ret(ret, "RK_MPI_VO_SetLayerAttr");

    ret = RK_MPI_VO_EnableLayer(DEFAULT_VO_LAYER);
    check_ret(ret, "RK_MPI_VO_EnableLayer");

    // 配置 VO 通道属性
    VO_CHN_ATTR_S vo_chn_attr;
    memset(&vo_chn_attr, 0, sizeof(VO_CHN_ATTR_S));
    vo_chn_attr.stRect.s32X = 0;
    vo_chn_attr.stRect.s32Y = 0;
    vo_chn_attr.stRect.u32Width = 1920;
    vo_chn_attr.stRect.u32Height = 1080;
    ret = RK_MPI_VO_SetChnAttr(DEFAULT_VO_LAYER, DEFAULT_VO_CHN, &vo_chn_attr);
    check_ret(ret, "RK_MPI_VO_SetChnAttr");

    ret = RK_MPI_VO_EnableChn(DEFAULT_VO_LAYER, DEFAULT_VO_CHN);
    check_ret(ret, "RK_MPI_VO_EnableChn");

    // 绑定 VDEC 到 VPSS
    MPP_CHN_S vdec_chn = {.enModId = RK_ID_VDEC, .s32DevId = 0, .s32ChnId = DEFAULT_VDEC_CHN};
    MPP_CHN_S vpss_grp_chn = {.enModId = RK_ID_VPSS, .s32DevId = DEFAULT_VPSS_GRP, .s32ChnId = DEFAULT_VPSS_CHN};
    ret = RK_MPI_SYS_Bind(&vdec_chn, &vpss_grp_chn);
    check_ret(ret, "RK_MPI_SYS_Bind(VDEC to VPSS)");

    // 绑定 VPSS 到 VO
    MPP_CHN_S vpss_chn = {.enModId = RK_ID_VPSS, .s32DevId = DEFAULT_VPSS_GRP, .s32ChnId = DEFAULT_VPSS_CHN};
    MPP_CHN_S vo_chn = {.enModId = RK_ID_VO, .s32DevId = DEFAULT_VO_LAYER, .s32ChnId = DEFAULT_VO_CHN};
    ret = RK_MPI_SYS_Bind(&vpss_chn, &vo_chn);
    check_ret(ret, "RK_MPI_SYS_Bind(VPSS to VO)");

     // 创建线程发送 H264 数据
    pthread_t h264_thread;
    struct h264_send_arg thread_data = {DEFAULT_VDEC_CHN, h264_file_path};
    ret = pthread_create(&h264_thread, NULL, send_h264_data, &thread_data);
    if (ret != 0) {
        perror("Failed to create thread");
        RK_MPI_VDEC_StopRecvStream(DEFAULT_VDEC_CHN);
        RK_MPI_VDEC_DestroyChn(DEFAULT_VDEC_CHN);
        RK_MPI_SYS_Exit();
        return EXIT_FAILURE;
    }

    // 等待发送 H264 数据的线程结束
    ret = pthread_join(h264_thread, NULL);
    if (ret != 0) {
        perror("Failed to join thread");
    }

    // 解除绑定
    ret = RK_MPI_SYS_UnBind(&vpss_chn, &vo_chn);
    check_ret(ret, "RK_MPI_SYS_UnBind(VPSS to VO)");
    ret = RK_MPI_SYS_UnBind(&vdec_chn, &vpss_grp_chn);
    check_ret(ret, "RK_MPI_SYS_UnBind(VDEC to VPSS)");

    // 释放资源
    ret = RK_MPI_VO_DisableChn(DEFAULT_VO_LAYER, DEFAULT_VO_CHN);
    check_ret(ret, "RK_MPI_VO_DisableChn");
    ret = RK_MPI_VO_DisableLayer(DEFAULT_VO_LAYER);
    check_ret(ret, "RK_MPI_VO_DisableLayer");
    ret = RK_MPI_VO_Disable(DEFAULT_VO_DEV);
    check_ret(ret, "RK_MPI_VO_Disable");
    ret = RK_MPI_VO_UnBindLayer(DEFAULT_VO_LAYER, DEFAULT_VO_DEV);
    check_ret(ret, "RK_MPI_VO_UnBindLayer");

    ret = RK_MPI_VPSS_StopGrp(DEFAULT_VPSS_GRP);
    check_ret(ret, "RK_MPI_VPSS_StopGrp");
    ret = RK_MPI_VPSS_DisableChn(DEFAULT_VPSS_GRP, DEFAULT_VPSS_CHN);
    check_ret(ret, "RK_MPI_VPSS_DisableChn");
    ret = RK_MPI_VPSS_DestroyGrp(DEFAULT_VPSS_GRP);
    check_ret(ret, "RK_MPI_VPSS_DestroyGrp");

    ret = RK_MPI_VDEC_StopRecvStream(DEFAULT_VDEC_CHN);
    check_ret(ret, "RK_MPI_VDEC_StopRecvStream");
    ret = RK_MPI_VDEC_DestroyChn(DEFAULT_VDEC_CHN);
    check_ret(ret, "RK_MPI_VDEC_DestroyChn");

    ret = RK_MPI_SYS_Exit();
    check_ret(ret, "RK_MPI_SYS_Exit");

    return 0;
}