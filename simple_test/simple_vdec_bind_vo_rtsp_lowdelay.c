#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_adec.h"
#include "rk_mpi_aenc.h"
#include "rk_mpi_ai.h"
#include "rk_mpi_ao.h"
#include "rk_mpi_avs.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_ivs.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_rgn.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_tde.h"
#include "rk_mpi_vdec.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"
#include "rkdemuxer.h"

#define VDEC_MAX_CHN              2
#define DEMUXER_TYPE_PASSIVE      0

static bool quit = false;
static int VoLayer[VDEC_MAX_CHN] = {0, -1};
static int VoDev[VDEC_MAX_CHN] = {0, -1};
static int VoChn[VDEC_MAX_CHN] = {0, 1};
static int IntfType[VDEC_MAX_CHN] = {2, -1};
static int enIntfType[VDEC_MAX_CHN] = {0, -1};
static int VoWidth[VDEC_MAX_CHN] = {1920, 1920};
static int VoHeight[VDEC_MAX_CHN] = {1080, 1080};
static int VoImageWidth[VDEC_MAX_CHN] = {3840, 1920};
static int VoImageHeight[VDEC_MAX_CHN] = {2160, 1080};

static int g_VoDevInitCnt[VO_MAX_DEV_NUM] = {0};
static int g_VoLayerInitCnt[VO_MAX_LAYER_NUM] = {0};
static int sys_bind = 0;

typedef void *DEMUXER_MW_PTR;

typedef void (*SIMPLE_DEMUXER_READ_PACKET_CALLBACK_FN)(DEMUXER_MW_PTR pHandle);

typedef struct {
    SIMPLE_DEMUXER_READ_PACKET_CALLBACK_FN pfnReadVideoPacketCallback;
    SIMPLE_DEMUXER_READ_PACKET_CALLBACK_FN pfnReadAudioPacketCallback;
} SIMPLE_DEMUXER_READ_PACKET_CALLBACK_S;

typedef struct {
    void *ptr;
    bool  readModeFlag;
    bool  videoEnableFlag;
    bool  audioEnableFlag;
    const char *transport;
    RK_U32 u32IoTimeout;
} SIMPLE_DEMUXER_INPUT_S;

typedef struct {
    RK_S32   totalTime;
    RK_CHAR *pVideoCodec;
    RK_S32   videoWidth;
    RK_S32   videoHeigh;
    RK_S8    VideoFormat;
    RK_S32   videoTimeBaseNum;
    RK_S32   videoTimeBaseDen;
    RK_S32   videoAvgFrameRate;
    RK_S64   videoFirstPTS;
    RK_CHAR *pAudioCodec;
    RK_S32   audioChannels;
    RK_S32   audioSampleRate;
    RK_S8    audioFormat;
    RK_S64   audioFirstPTS;
    RK_S32   audioTimeBaseNum;
    RK_S32   audioTimeBaseDen;
    SIMPLE_DEMUXER_READ_PACKET_CALLBACK_S pstReadPacketCallback;
} SIMPLE_DEMUXER_PARAM_S;

typedef struct {
    bool     bVideoSendStreamFail;
    RK_S32   videoStreamCount;
    RK_S32   videoAvgFrameRate;
    RK_S32   vdecChnIndex;
    RK_S32   vdecChnFd;
} SIMPLE_PLAYER_S;

typedef struct {
    void * demuxer_handle[VDEC_MAX_CHN];
    SIMPLE_DEMUXER_INPUT_S stDemuxerInput[VDEC_MAX_CHN];
    SIMPLE_DEMUXER_PARAM_S stDemuxerParam[VDEC_MAX_CHN];
    SIMPLE_PLAYER_S stPlayer[VDEC_MAX_CHN];
} SIMPLE_LOWDELAY_CTX_S;

static SIMPLE_LOWDELAY_CTX_S ctx;

static void sigterm_handler(int sig) {
    fprintf(stderr, "signal %d\n", sig);
    quit = true;
}

static RK_S32 BufferFree(void *opaque) {
    if (opaque) {
        free(opaque);
        opaque = NULL;
    }
    return 0;
}

static RK_S32 VdecPollEvent(RK_S32 timeoutMsec, RK_S32 fd) {
  RK_S32 num_fds = 1;
  struct pollfd pollFds[num_fds];
  RK_S32 ret = 0;

  RK_ASSERT(fd > 0);
  memset(pollFds, 0, sizeof(pollFds));
  pollFds[0].fd = fd;
  pollFds[0].events = (POLLPRI | POLLIN | POLLERR | POLLNVAL | POLLHUP);

  ret = poll(pollFds, num_fds, timeoutMsec);
  if (ret > 0 && (pollFds[0].revents & (POLLERR | POLLNVAL | POLLHUP))) {
    RK_LOGE("fd[%d] polled error", fd);
    return -1;
  }

  return ret;
}

static void *GetMediaBuffer0(void *arg) {
    printf("========%s========\n", __func__);
    int s32Ret;
    RK_S32 waitTime = 1000;
    VIDEO_FRAME_INFO_S stFrame;

    while (!quit) {
        s32Ret = VdecPollEvent(waitTime, ctx.stPlayer[0].vdecChnFd);
        if (s32Ret < 0)
            continue;
        s32Ret = RK_MPI_VDEC_GetFrame(0, &stFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {

            RK_MPI_VO_SendFrame(VoLayer[0], VoChn[0], &stFrame, -1);

            // 7.release the frame
            s32Ret = RK_MPI_VDEC_ReleaseFrame(0, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VDEC_ReleaseFrame fail %x", s32Ret);
            }
        } else {
            RK_LOGD("RK_MPI_VDEC_GetFrame timeout %x", s32Ret);
        }
    }
    return NULL;
}

static void *GetMediaBuffer1(void *arg) {
    printf("========%s========\n", __func__);
    int s32Ret;
    RK_S32 waitTime = 1000;
    VIDEO_FRAME_INFO_S stFrame;

    while (!quit) {
        s32Ret = VdecPollEvent(waitTime, ctx.stPlayer[1].vdecChnFd);
        if (s32Ret < 0)
            continue;
        s32Ret = RK_MPI_VDEC_GetFrame(1, &stFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {

            RK_MPI_VO_SendFrame(VoLayer[1], VoChn[1], &stFrame, -1);

            // 7.release the frame
            s32Ret = RK_MPI_VDEC_ReleaseFrame(0, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VDEC_ReleaseFrame fail %x", s32Ret);
            }
        } else {
            RK_LOGD("RK_MPI_VDEC_GetFrame timeout %x", s32Ret);
        }
    }
    return NULL;
}

static int simple_demuxer_init(DEMUXER_MW_PTR *demuxerCfg, SIMPLE_DEMUXER_INPUT_S *demuxerInput) {
    int ret = 0;
    DemuxerInput stDemuxerInput;

    if (demuxerInput == NULL) {
        RK_LOGE("demuxerInput, return fail");
        return -1;
    }

    stDemuxerInput.ptr = demuxerInput->ptr;
    stDemuxerInput.s8ReadModeFlag = demuxerInput->readModeFlag;
    stDemuxerInput.s8VideoEnableFlag = demuxerInput->videoEnableFlag;
    stDemuxerInput.s8AudioEnableFlag = demuxerInput->audioEnableFlag;
    stDemuxerInput.transport = demuxerInput->transport;
    stDemuxerInput.network_io_timeout = demuxerInput->u32IoTimeout;

    ret = rkdemuxer_init(demuxerCfg, &stDemuxerInput);
    return ret;
}

static int simple_demuxer_deinit(DEMUXER_MW_PTR *demuxerCfg) {
    int ret = 0;
    if (demuxerCfg == NULL) {
        RK_LOGE("demuxerCfg is NULL, return fail");
        return -1;
    }
    rkdemuxer_deinit(demuxerCfg);
    return ret;
}

static int simple_demuxer_get_param(DEMUXER_MW_PTR demuxerCfg, const char *inputName, SIMPLE_DEMUXER_PARAM_S *demuxerParam) {
    int ret = 0;
    DemuxerParam tempDemuxerParam;

    if (demuxerCfg == NULL || inputName == NULL) {
        RK_LOGE("demuxerCfg or inputName is NULL, return fail");
        return -1;
    }

    tempDemuxerParam.pstReadPacketCallback.read_video_packet = demuxerParam->pstReadPacketCallback.pfnReadVideoPacketCallback;
    tempDemuxerParam.pstReadPacketCallback.read_audio_packet = demuxerParam->pstReadPacketCallback.pfnReadAudioPacketCallback;
    ret = rkdemuxer_get_param(demuxerCfg, inputName, &tempDemuxerParam);
    if (ret) {
        RK_LOGE("SIMPLE_DEMUXER_GetParam failed");
        return -1;
    }

    demuxerParam->totalTime = tempDemuxerParam.s32TotalTime;
    demuxerParam->pVideoCodec = (char *)tempDemuxerParam.pVideoCodec;
    demuxerParam->videoWidth = tempDemuxerParam.s32VideoWidth;
    demuxerParam->videoHeigh = tempDemuxerParam.s32VideoHeigh;
    demuxerParam->VideoFormat = tempDemuxerParam.s8VideoFormat;
    demuxerParam->videoTimeBaseNum = tempDemuxerParam.s32VideoTimeBaseNum;
    demuxerParam->videoTimeBaseDen = tempDemuxerParam.s32VideoTimeBaseDen;
    demuxerParam->videoFirstPTS = tempDemuxerParam.s64VideoFirstPTS;
    demuxerParam->videoAvgFrameRate = tempDemuxerParam.s32VideoAvgFrameRate;
    demuxerParam->pAudioCodec = (char *)tempDemuxerParam.pAudioCodec;
    demuxerParam->audioChannels = tempDemuxerParam.s32AudioChannels;
    demuxerParam->audioSampleRate = tempDemuxerParam.s32AudioSampleRate;
    demuxerParam->audioFormat = tempDemuxerParam.s8AudioFormat;
    demuxerParam->audioFirstPTS = tempDemuxerParam.s64AudioFirstPTS;
    demuxerParam->audioTimeBaseNum = tempDemuxerParam.s32AudioTimeBaseNum;
    demuxerParam->audioTimeBaseDen = tempDemuxerParam.s32AudioTimeBaseDen;

    return 0;
}

static int simple_demuxer_read_packet_start(DEMUXER_MW_PTR demuxerCfg, RK_S64 startPts) {
    if (demuxerCfg == NULL) {
        RK_LOGE("demuxerCfg is NULL");
        return -1;
    }
    RK_S32 ret = rkdemuxer_read_packet_start(demuxerCfg, startPts);
    if (ret) {
        RK_LOGE("simple_demuxer_read_packet_start failed");
        return -1;
    }

    return 0;
}

static int simple_demuxer_read_packet_stop(DEMUXER_MW_PTR demuxerCfg) {
    if (demuxerCfg == NULL) {
        RK_LOGE("demuxerCfg is NULL");
        return -1;
    }
    RK_S32 ret = rkdemuxer_read_packet_stop(demuxerCfg);
    if (ret) {
        RK_LOGE("simple_demuxer_read_packet_stop failed");
        return -1;
    }

    return 0;
}

static void DoPullDemuxerVideoPacket(void *pHandle) {
    DemuxerPacket *pstDemuxerPacket = (DemuxerPacket *)pHandle;
    SIMPLE_PLAYER_S *pstPlayer = (SIMPLE_PLAYER_S *)pstDemuxerPacket->ptr;
    RK_S32 ret = 0;
    VDEC_STREAM_S stStream;
    MB_BLK buffer = NULL;
    MB_EXT_CONFIG_S stMbExtConfig;

    memset(&stMbExtConfig, 0, sizeof(MB_EXT_CONFIG_S));
    stMbExtConfig.pFreeCB = BufferFree;
    stMbExtConfig.pOpaque = (RK_VOID *)pstDemuxerPacket->s8PacketData;
    stMbExtConfig.pu8VirAddr = (RK_U8 *)pstDemuxerPacket->s8PacketData;
    stMbExtConfig.u64Size = pstDemuxerPacket->s32PacketSize;

    RK_MPI_SYS_CreateMB(&buffer, &stMbExtConfig);

    stStream.u64PTS = pstDemuxerPacket->s64Pts;
    stStream.pMbBlk = buffer;
    stStream.u32Len = pstDemuxerPacket->s32PacketSize;
    stStream.bEndOfStream = pstDemuxerPacket->s8EofFlag ? RK_TRUE : RK_FALSE;
    stStream.bEndOfFrame = pstDemuxerPacket->s8EofFlag ? RK_TRUE : RK_FALSE;
    stStream.bBypassMbBlk = RK_TRUE;

    if (!pstPlayer->bVideoSendStreamFail || pstDemuxerPacket->s8SpecialFlag)
        ret = RK_MPI_VDEC_SendStream(pstPlayer->vdecChnIndex, &stStream, 0);
    else
        ret = false;

    if (ret) {
        pstPlayer->bVideoSendStreamFail = true;
    } else {
        pstPlayer->bVideoSendStreamFail = false;
        pstPlayer->videoStreamCount++;
    }
    RK_MPI_MB_ReleaseMB(stStream.pMbBlk);

      return;
}

int vdec_init(int channelId, int width, int height, int *chnFd) {
    int ret = 0;
    VDEC_CHN_ATTR_S vdec_chn_attr;
    memset(&vdec_chn_attr, 0, sizeof(VDEC_CHN_ATTR_S));
    vdec_chn_attr.enType = RK_VIDEO_ID_AVC;
    vdec_chn_attr.enMode = VIDEO_MODE_FRAME;
    vdec_chn_attr.u32PicWidth = width;
    vdec_chn_attr.u32PicHeight = height;
    vdec_chn_attr.u32FrameBufCnt = 6;
    vdec_chn_attr.u32StreamBufCnt = 6;
    vdec_chn_attr.u32FrameBufDepth = 6;
    ret = RK_MPI_VDEC_CreateChn(channelId, &vdec_chn_attr);
    if (chnFd)
        *chnFd = RK_MPI_VDEC_GetFd(channelId);
    ret = RK_MPI_VDEC_StartRecvStream(channelId);
    return ret;
}

int vdec_deinit(int channelId) {
    int ret = 0;
    ret = RK_MPI_VDEC_StopRecvStream(channelId);
    if (ctx.stPlayer[1].vdecChnFd > 0)
        ret = RK_MPI_VDEC_CloseFd(channelId);
    ret = RK_MPI_VDEC_DestroyChn(channelId);
    return ret;
}


static int vo_init(int VoLayer, int VoDev, int VoChn, int Width, int Height, int ImageWidth, int ImageHeight, int IntfType) {
    int ret = RK_SUCCESS;
    VO_PUB_ATTR_S            stVoPubAttr;
    VO_VIDEO_LAYER_ATTR_S    stLayerAttr;
    VO_CHN_ATTR_S            stChnAttr;

    if (VoLayer == -1 || VoDev == -1) {
        return RK_FAILURE;
    }
    if (g_VoLayerInitCnt[VoLayer] == 0) {
        if (g_VoDevInitCnt[VoDev] == 0)
            ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_VIDEO);
        else
            ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_GRAPHIC);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_BindLayer failed, ret = %x", ret);
            return ret;
        }
    }

    memset(&stVoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
    memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
    memset(&stChnAttr, 0, sizeof(VO_CHN_ATTR_S));

    if (IntfType != VO_INTF_DEFAULT)
        stVoPubAttr.enIntfType = IntfType;
    else
        stVoPubAttr.enIntfType = VO_INTF_DEFAULT;
    stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

    if (g_VoDevInitCnt[VoDev] == 0) {
        ret = RK_MPI_VO_SetPubAttr(VoDev, &stVoPubAttr);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_SetPubAttr failed, ret = %x", ret);
            return ret;
        }

        ret = RK_MPI_VO_Enable(VoDev);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_Enable failed, ret = %x", ret);
            return ret;
        }
    }
    g_VoDevInitCnt[VoDev]++;

    if (g_VoLayerInitCnt[VoLayer] == 0) {
        /* Enable Layer */
        stLayerAttr.enPixFormat      = RK_FMT_RGB888;
        stLayerAttr.enCompressMode   = COMPRESS_MODE_NONE;
        stLayerAttr.stDispRect.s32X  = 0;
        stLayerAttr.stDispRect.s32Y  = 0;
        stLayerAttr.stDispRect.u32Width   = Width;
        stLayerAttr.stDispRect.u32Height  = Height;
        stLayerAttr.stImageSize.u32Width  = ImageWidth;
        stLayerAttr.stImageSize.u32Height = ImageHeight;
        stLayerAttr.u32DispFrmRt          = 60;
#ifndef RV1106_RV1103
        stLayerAttr.bBypassFrame          = true;
#endif
        ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_SetLayerAttr failed, ret = %x", ret);
            return ret;
        }

        RK_MPI_VO_SetLayerSpliceMode(VoLayer, VO_SPLICE_MODE_RGA);

        ret = RK_MPI_VO_EnableLayer(VoLayer);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_EnableLayer failed, ret = %x", ret);
            return ret;
        }
    }
    g_VoLayerInitCnt[VoLayer]++;

    stChnAttr.stRect.s32X = 0;
    stChnAttr.stRect.s32Y = 0;
    stChnAttr.stRect.u32Width = Width;
    stChnAttr.stRect.u32Height = Height;
    stChnAttr.u32FgAlpha = 255;
    stChnAttr.u32BgAlpha = 0;
    stChnAttr.enMirror = MIRROR_NONE;
    stChnAttr.enRotation = ROTATION_0;
    stChnAttr.u32Priority = 1;
    // stChnAttr.u32MaxChnQueue = 5;

    ret = RK_MPI_VO_SetChnAttr(VoLayer, VoChn, &stChnAttr);
    if (ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_SetChnAttr failed, ret = %x", ret);
        return ret;
    }

    ret = RK_MPI_VO_EnableChn(VoLayer, VoChn);
    if (ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_EnableChn failed, ret = %x", ret);
        return ret;
    }

    RK_LOGE("Create vo [dev: %d, layer: %d, chn: %d] success!",
                VoDev, VoLayer, VoChn);
    return ret;
}

static int vo_deinit(int VoLayer, int VoDev, int VoChn) {
    int ret = 0;

    if (VoLayer == -1 || VoDev == -1) {
        return RK_FAILURE;
    }
    ret = RK_MPI_VO_DisableChn(VoLayer, VoChn);
    if (ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_DisableChn failed, ret = %x", ret);
        return ret;
    }

    if (g_VoLayerInitCnt[VoLayer] == 1) {
        ret = RK_MPI_VO_DisableLayer(VoLayer);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_DisableLayer failed, ret = %x", ret);
            return ret;
        }
    }

    if (g_VoDevInitCnt[VoDev] == 1) {
        ret = RK_MPI_VO_Disable(VoDev);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_Disable failed, ret = %x", ret);
            return ret;
        }
    }
    g_VoDevInitCnt[VoDev]--;

    if (g_VoLayerInitCnt[VoLayer] == 1) {
        ret = RK_MPI_VO_UnBindLayer(VoLayer, VoDev);
        if (ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VO_UnBindLayer failed, ret = %x", ret);
            return ret;
        }
    }
    g_VoLayerInitCnt[VoLayer]--;

    RK_LOGI("Destroy vo [dev: %d, layer: %d, chn: %d] success!",
            VoDev, VoLayer, VoChn);
    return ret;
}

static int vo_get_en_intf_type(int type) {
    int enIntfType = 0;

    if (type == 1)
        enIntfType = VO_INTF_MIPI;
    else if (type == 2)
        enIntfType = VO_INTF_HDMI;
    else if (type == 3)
        enIntfType = VO_INTF_LCD;
    else
        enIntfType = VO_INTF_DEFAULT;

    return enIntfType;
}

#define OPT_IMAGE_WIDTH_1      256
#define OPT_IMAGE_HEIGHT_1     257
#define OPT_DISP_WIDTH_1       258
#define OPT_DISP_HEIGHT_1      259
#define OPT_VO_LAYER_1         260
#define OPT_VO_DEV_1           261
#define OPT_VO_INTF_TYPE_1     262

static struct option long_optstr[] = {
    {"input", required_argument, 0, 'i'},
    {"input1", required_argument, 0, 'I'},
    {"image_width", required_argument, 0, 'w'},
    {"image_height", required_argument, 0, 'h'},
    {"disp_width", required_argument, 0, 'W'},
    {"disp_height", required_argument, 0, 'H'},
    {"vo_layer", required_argument, 0, 'l'},
    {"vo_dev", required_argument, 0, 'd'},
    {"vo_intf_type", required_argument, 0, 't'},
    {"help", no_argument, 0, 'h'},
    {"image_width_1", required_argument, 0, OPT_IMAGE_WIDTH_1},
    {"image_height_1", required_argument, 0, OPT_IMAGE_HEIGHT_1},
    {"disp_width_1", required_argument, 0, OPT_DISP_WIDTH_1},
    {"disp_height_1", required_argument, 0, OPT_DISP_HEIGHT_1},
    {"vo_layer_1", required_argument, 0, OPT_VO_LAYER_1},
    {"vo_dev_1", required_argument, 0, OPT_VO_DEV_1},
    {"vo_intf_type_1", required_argument, 0, OPT_VO_INTF_TYPE_1},
    {0, 0, 0, 0}
};

static RK_CHAR optstr[] = "?::w:h:i:I:l:d:t:W:H:";
static void print_usage(const RK_CHAR *name) {
    printf("usage example:\n");
    printf("\t%s simple_vdec_bind_vo_rtsp_lowdelay  -i xxx.mp4 -I 0 -l 0 -d 0\n", name);
    printf("\t-w | --Image width: width, Default:1920\n");
    printf("\t-h | --Image heght: height, Default:1080\n");
    printf("\t-l | --layer: Vo layer, Default 0. \n");
    printf("\t-d | --device: Vo device, Default 0. \n");
    printf("\t-t | --type: Vo en intf type, Default 0. \n");
    printf("\t-W | --width: Vo display width, Default 0. \n");
    printf("\t-H | --height: Vo display height, Default 0. \n");
}

int main(int argc, char *argv[]) {
    RK_S32 s32Ret = RK_FAILURE;
    int c;
    int ret = -1;
    int use_chn_cnt = VDEC_MAX_CHN;
    MPP_CHN_S stvdecChn[VDEC_MAX_CHN], stvoChn[VDEC_MAX_CHN];
    int transport = 0, ioTimeout = 0;
    char *url[VDEC_MAX_CHN];

    pthread_t main_thread;
    pthread_t main_thread1;

    memset(&ctx, 0, sizeof(SIMPLE_LOWDELAY_CTX_S));

    while ((c = getopt_long(argc, argv, optstr, long_optstr, NULL)) != -1) {
        switch (c) {
        case 'w':
            VoImageWidth[0] = atoi(optarg);
            break;
        case 'h':
            VoImageHeight[0] = atoi(optarg);
            break;
        case 'W':
            VoWidth[0] = atoi(optarg);
            break;
        case 'H':
            VoHeight[0] = atoi(optarg);
            break;
        case 'i':
            url[0] = optarg;
            break;
        case 'I':
            url[1] = optarg;
            break;
        case 'l':
            VoLayer[0] = atoi(optarg);
            break;
        case 'd':
            VoDev[0] = atoi(optarg);
            break;
        case 't':
            IntfType[0] = atoi(optarg);
            break;
        case OPT_IMAGE_WIDTH_1:
            VoImageWidth[1] = atoi(optarg);
            break;
        case OPT_IMAGE_HEIGHT_1:
            VoImageHeight[1] = atoi(optarg);
            break;
        case OPT_DISP_WIDTH_1:
            VoWidth[1] = atoi(optarg);
            break;
        case OPT_DISP_HEIGHT_1:
            VoHeight[1] = atoi(optarg);
            break;
        case OPT_VO_LAYER_1:
            VoLayer[1] = atoi(optarg);
            break;
        case OPT_VO_DEV_1:
            VoDev[1] = atoi(optarg);
            break;
        case OPT_VO_INTF_TYPE_1:
            IntfType[1] = atoi(optarg);
            break;

        case '?':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    signal(SIGINT, sigterm_handler);

    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        RK_LOGE("rk mpi sys init fail!");
        goto __FAILED;
    }

    if (VoLayer[1] == -1 || VoDev[1] == -1)
        use_chn_cnt = 1;
    for (int i = 0; i < use_chn_cnt; i++) {
        enIntfType[i] = vo_get_en_intf_type(IntfType[i]);
        printf("VoWidth[%d] %d, VoHeight[%d] %d\n", i, VoWidth[i], i, VoHeight[i]);
        vo_init(VoLayer[i], VoDev[i], VoChn[i], VoWidth[i], VoHeight[i], VoImageWidth[i], VoImageHeight[i], enIntfType[i]);
        ctx.stPlayer[i].vdecChnIndex = i;
        ctx.stDemuxerInput[i].ptr = (void *) (&ctx.stPlayer[i]);
        ctx.stDemuxerInput[i].readModeFlag = DEMUXER_TYPE_PASSIVE;
        ctx.stDemuxerInput[i].videoEnableFlag = 1;
        ctx.stDemuxerInput[i].audioEnableFlag = 0;
        ctx.stDemuxerInput[i].transport = "udp";
        ctx.stDemuxerInput[i].u32IoTimeout = ioTimeout;
        simple_demuxer_init(&ctx.demuxer_handle[i], &ctx.stDemuxerInput[i]);
        ctx.stDemuxerParam[i].pstReadPacketCallback.pfnReadVideoPacketCallback = DoPullDemuxerVideoPacket;
        simple_demuxer_get_param(ctx.demuxer_handle[i], url[i], &ctx.stDemuxerParam[i]);
        vdec_init(i, ctx.stDemuxerParam[i].videoWidth, ctx.stDemuxerParam[i].videoHeigh, &ctx.stPlayer[i].vdecChnFd);
        simple_demuxer_read_packet_start(ctx.demuxer_handle[i], 0);
    }

    if (sys_bind == 1) {
        printf("====RK_MPI_SYS_Bind vdec to vo====\n");
        for (int i = 0; i < use_chn_cnt; i++) {
            stvdecChn[i].enModId = RK_ID_VDEC;
            stvdecChn[i].s32DevId = 0;
            stvdecChn[i].s32ChnId = i;

            stvoChn[i].enModId = RK_ID_VO;
            stvoChn[i].s32DevId = VoLayer[i];
            stvoChn[i].s32ChnId = VoChn[i];

            s32Ret = RK_MPI_SYS_Bind(&stvdecChn[i], &stvoChn[i]);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("bind vdec to vo failed");
                goto __FAILED;
            }
        }
    } else {
        pthread_create(&main_thread, NULL, GetMediaBuffer0, NULL);
        if (use_chn_cnt == VDEC_MAX_CHN) {
            pthread_create(&main_thread1, NULL, GetMediaBuffer1, NULL);
        }
    }

    while (!quit) {
        usleep(50000);
    }

    for (int i = 0; i < use_chn_cnt; i++)
        simple_demuxer_read_packet_stop(ctx.demuxer_handle[i]);

    if (sys_bind == 1) {
        for (int i = 0; i < use_chn_cnt; i++) {
            s32Ret = RK_MPI_SYS_UnBind(&stvdecChn[i], &stvoChn[i]);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
            }
            simple_demuxer_deinit(&ctx.demuxer_handle[0]);
        }
    } else {
        pthread_join(main_thread, NULL);
        if (use_chn_cnt == VDEC_MAX_CHN) {
            pthread_join(main_thread1, NULL);
        }
    }

    for (int i = 0; i < use_chn_cnt; i++) {
        vdec_deinit(i);
        vo_deinit(VoLayer[i], VoDev[i], VoChn[i]);
    }

    RK_MPI_VO_CloseFd();

    ret = 0;
__FAILED:
    RK_LOGE("test running exit:%d", s32Ret);
    RK_MPI_SYS_Exit();

    return ret;
}
