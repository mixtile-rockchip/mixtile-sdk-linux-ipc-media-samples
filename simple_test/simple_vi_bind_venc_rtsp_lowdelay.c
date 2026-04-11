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

#include "rtsp_demo.h"
#ifdef RV1126_RV1109
#include <rk_aiq_user_api_camgroup.h>
#include <rk_aiq_user_api_imgproc.h>
#include <rk_aiq_user_api_sysctl.h>
#else
#include <rk_aiq_user_api2_camgroup.h>
#include <rk_aiq_user_api2_imgproc.h>
#include <rk_aiq_user_api2_sysctl.h>
#endif

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

#define MAX_AIQ_CTX 8

#include <stdatomic.h>

static RK_S32 g_s32FrameCnt = -1;
static RK_U32 g_u32Bitrate = 10 * 1024;
static RK_U32 g_u32SliceSplit = 0;
static RK_S32 g_s32ChnCnt = 1;
static RK_S32 g_s32FrameRate = 60;
static bool quit = false;
static RK_S32 g_rtsp_mode = 1;
static char *g_dst_ip = "192.168.1.103";
static char *g_VIEntityName = "/dev/video0";

rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;
static rtsp_session_handle g_rtsp_session1;

static void sigterm_handler(int sig) {
    fprintf(stderr, "signal %d\n", sig);
    quit = true;
}

RK_U64 TEST_COMM_GetNowUs() {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

static void *GetMediaBuffer0(void *arg) {
    (void)arg;
    printf("========%s========\n", __func__);
    void *pData = RK_NULL;
    int loopCount = 0;
    int s32Ret;
    RK_U64 nowUs;

    VENC_STREAM_S stFrame;
    stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

    while (!quit) {
        s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 200000);
        if (s32Ret == RK_SUCCESS) {
            nowUs = TEST_COMM_GetNowUs();
            RK_LOGI("VENC GET stream chn:0, loopCount:%d enc->seq:%d wd:%d frame flag:%d, pts=%lld delay=%lldus\n",
                    loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len, stFrame.pstPack->stPackInfo[0].u32PackType.enH264EType,
                    stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);
            if (g_rtsp_mode == 1) {
                if (g_rtsplive && g_rtsp_session) {
                    pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                    rtsp_tx_video(g_rtsp_session, pData, stFrame.pstPack->u32Len,
                                stFrame.pstPack->u64PTS);
                    rtsp_do_event(g_rtsplive);
                }
            }

            nowUs = TEST_COMM_GetNowUs();

            RK_LOGI("chn:0, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n",
                    loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len,
                    stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);

            s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
            }
            loopCount++;
        }

        if ((g_s32FrameCnt >= 0) && (loopCount > g_s32FrameCnt)) {
            quit = true;
            break;
        }
    }
    printf("\n======exit %s=======\n", __func__);
    free(stFrame.pstPack);
    return NULL;
}

static void *GetMediaBuffer1(void *arg) {
    (void)arg;
    printf("========%s========\n", __func__);
    void *pData = RK_NULL;
    int loopCount = 0;
    int s32Ret;

    VENC_STREAM_S stFrame;
    stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

    while (!quit) {
        s32Ret = RK_MPI_VENC_GetStream(1, &stFrame, 200000);
        if (s32Ret == RK_SUCCESS) {
            if (g_rtsp_mode == 1) {
                if (g_rtsplive && g_rtsp_session1) {
                    pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                    rtsp_tx_video(g_rtsp_session1, pData, stFrame.pstPack->u32Len,
                                stFrame.pstPack->u64PTS);
                    rtsp_do_event(g_rtsplive);
                }
            }

            RK_U64 nowUs = TEST_COMM_GetNowUs();

            RK_LOGI("chn:1, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n",
                    loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len,
                    stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);

            s32Ret = RK_MPI_VENC_ReleaseStream(1, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
            }
            loopCount++;
        }

        if ((g_s32FrameCnt >= 0) && (loopCount > g_s32FrameCnt)) {
            quit = true;
            break;
        }
    }
    printf("\n======exit %s=======\n", __func__);
    free(stFrame.pstPack);
    return NULL;
}

static void *GetVIBufferSendVPSS(void *arg) {
    printf("========%s========\n", __func__);
    int s32Ret;
    RK_S32 waitTime = 1000;
    int pipeId = 0;
    VIDEO_FRAME_INFO_S stViFrame;
    int loopCount = 0;
    memset(&stViFrame, 0, sizeof(stViFrame));

    while (!quit) {
        s32Ret = RK_MPI_VI_GetChnFrame(pipeId, 0, &stViFrame, waitTime);
        if (s32Ret == RK_SUCCESS) {
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            RK_LOGI("VI chn:0, width %ld, pts=%llu, delay=%lluus\n",
                stViFrame.stVFrame.u32Width, stViFrame.stVFrame.u64PTS, (nowUs - stViFrame.stVFrame.u64PTS));

            // 针对VI提前出帧，需要增加延时，使得VPSS处理完的耗时刚好是VI这一帧内容结束的时候，如果计算结果小于等于0，则无需延时
            // echo_wait_lines 是提前出帧的行数，根据VO低延时文档配置的
            // usleep_time = frame_time - frame_time * (echo_wait_lines - 32) / height - vpss_costtime
#if defined(RK3576) || defined(RK3572)
            usleep(10000); // echo_wait_lines = 32  vpss_costtime = 6ms
#elif defined(RK3588)
            usleep(2000);  // echo_wait_lines = 1112  vpss_costtime = 6ms
#endif
            RK_MPI_VPSS_SendFrame(0, 0, &stViFrame, -1);

            // 7.release the frame
            s32Ret = RK_MPI_VI_ReleaseChnFrame(pipeId, 0, &stViFrame);
            if (s32Ret != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
            }
            loopCount++;
        } else {
            RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
        }
    }

    return NULL;
}

static RK_S32 test_venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
    printf("========%s========\n", __func__);
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_ATTR_S stAttr;
    VENC_RC_PARAM_S stRcParam;
    memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

    if (enType == RK_VIDEO_ID_AVC) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        stAttr.stRcAttr.stH264Cbr.u32BitRate = g_u32Bitrate;
        if (g_s32FrameRate == 30) {
            stAttr.stRcAttr.stH264Cbr.u32Gop = 30;
            stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = 30;
            stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = 30;
            stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        } else {
            stAttr.stRcAttr.stH264Cbr.u32Gop = g_s32FrameRate;
            stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = 60;
            stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = g_s32FrameRate;
            stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        }
        if (chnId == 1) {
            stAttr.stRcAttr.stH264Cbr.u32BitRate = g_u32Bitrate / 4;
            stAttr.stRcAttr.stH264Cbr.u32Gop = 30;
            stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = 30;
            stAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = 30;
            stAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
        }
    } else if (enType == RK_VIDEO_ID_HEVC) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        stAttr.stRcAttr.stH265Cbr.u32BitRate = g_u32Bitrate;
        if (g_s32FrameRate == 30) {
            stAttr.stRcAttr.stH265Cbr.u32Gop = 30;
            stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 30;
            stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = 30;
            stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
        } else {
            stAttr.stRcAttr.stH265Cbr.u32Gop = g_s32FrameRate;
            stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 60;
            stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = g_s32FrameRate;
            stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
        }
        if (chnId == 1){
            stAttr.stRcAttr.stH265Cbr.u32BitRate = g_u32Bitrate / 4;
            stAttr.stRcAttr.stH265Cbr.u32Gop = 30;
            stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = 30;
            stAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = 30;
            stAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
        }
    } else if (enType == RK_VIDEO_ID_MJPEG) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
        stAttr.stRcAttr.stMjpegCbr.u32BitRate = g_u32Bitrate;
        if (g_s32FrameRate == 30) {
            stAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateNum = 30;
            stAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateNum = 30;
            stAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateDen = 1;
        } else {
            stAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateNum = 60;
            stAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateNum = g_s32FrameRate;
            stAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateDen = 1;
        }
        if (chnId == 1) {
            stAttr.stRcAttr.stMjpegCbr.u32BitRate = g_u32Bitrate / 4;
            stAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateNum = 30;
            stAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateDen = 1;
            stAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateNum = 30;
            stAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateDen = 1;
        }
    }

    stAttr.stVencAttr.enType = enType;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    if (enType == RK_VIDEO_ID_AVC)
        stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32VirWidth = width;
    stAttr.stVencAttr.u32VirHeight = height;
    stAttr.stVencAttr.u32StreamBufCnt = 4;
    stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
    stAttr.stVencAttr.enMirror = MIRROR_NONE;

    RK_MPI_VENC_CreateChn(chnId, &stAttr);

    if (g_u32SliceSplit) {
        VENC_SLICE_SPLIT_S stSliceSplit;
        RK_MPI_VENC_GetSliceSplit(chnId, &stSliceSplit);
        stSliceSplit.bSplitEnable = RK_TRUE;
        stSliceSplit.u32SplitMode = 3;
        stSliceSplit.u32SplitSize = 120;
        RK_MPI_VENC_SetSliceSplit(chnId, &stSliceSplit);
    }

    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;
    RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

    if (stAttr.stVencAttr.enType == RK_VIDEO_ID_AVC) {
        memset(&stRcParam, 0, sizeof(VENC_RC_PARAM_S));
        stRcParam.s32FirstFrameStartQp = 36;
        stRcParam.stParamH264.u32StepQp = 4;
        stRcParam.stParamH264.u32MinQp = 10;
        stRcParam.stParamH264.u32MaxQp = 51;
        stRcParam.stParamH264.u32MinIQp = 30;
        stRcParam.stParamH264.u32MaxIQp = 51;
        stRcParam.stParamH264.s32DeltIpQp = -2;
        stRcParam.stParamH264.s32MaxReEncodeTimes = 2;
        stRcParam.stParamH264.u32FrmMinQp = 10;
        stRcParam.stParamH264.u32FrmMaxQp = 51;
        stRcParam.stParamH264.u32FrmMinIQp = 30;
        stRcParam.stParamH264.u32FrmMaxIQp = 51;
        RK_MPI_VENC_SetRcParam(chnId, &stRcParam);
    } else if (stAttr.stVencAttr.enType == RK_VIDEO_ID_HEVC) {
        memset(&stRcParam, 0, sizeof(VENC_RC_PARAM_S));
        stRcParam.s32FirstFrameStartQp = 36;
        stRcParam.stParamH265.u32StepQp = 4;
        stRcParam.stParamH265.u32MinQp = 10;
        stRcParam.stParamH265.u32MaxQp = 51;
        stRcParam.stParamH265.u32MinIQp = 30;
        stRcParam.stParamH265.u32MaxIQp = 51;
        stRcParam.stParamH265.s32DeltIpQp = -2;
        stRcParam.stParamH265.s32MaxReEncodeTimes = 2;
        stRcParam.stParamH265.u32FrmMinQp = 10;
        stRcParam.stParamH265.u32FrmMaxQp = 51;
        stRcParam.stParamH265.u32FrmMinIQp = 30;
        stRcParam.stParamH265.u32FrmMaxIQp = 51;
        RK_MPI_VENC_SetRcParam(chnId, &stRcParam);
    }

    return 0;
}

int vi_dev_init() {
    printf("%s\n", __func__);
    int ret = 0;
    int devId = 0;
    int pipeId = devId;

    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    memset(&stBindPipe, 0, sizeof(stBindPipe));
    // 0. get dev config status
    ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        // 0-1.config dev
        ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_SetDevAttr %x\n", ret);
            return -1;
        }
    } else {
        printf("RK_MPI_VI_SetDevAttr already\n");
    }
    // 1.get dev enable status
    ret = RK_MPI_VI_GetDevIsEnable(devId);
    if (ret != RK_SUCCESS) {
        // 1-2.enable dev
        ret = RK_MPI_VI_EnableDev(devId);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_EnableDev %x\n", ret);
            return -1;
        }
        // 1-3.bind dev/pipe
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
        if (ret != RK_SUCCESS) {
            printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
            return -1;
        }
    } else {
        printf("RK_MPI_VI_EnableDev already\n");
    }

    return 0;
}

int vi_chn_init(int channelId, int width, int height) {
    int ret;
    int buf_cnt = 6;
    // VI init
    VI_CHN_ATTR_S vi_chn_attr;
    memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
    vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
    vi_chn_attr.stIspOpt.enMemoryType =
           VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
#ifdef RK3588
    vi_chn_attr.stIspOpt.bNoUseLibV4L2 = RK_TRUE;
    vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_MMAP;
#endif
    memcpy(&vi_chn_attr.stIspOpt.aEntityName, g_VIEntityName, strlen(g_VIEntityName));
    if (g_s32FrameRate == 30) {
        vi_chn_attr.stFrameRate.s32SrcFrameRate = 60;
        vi_chn_attr.stFrameRate.s32DstFrameRate = 30;
        printf("============set src framerate %d, set dst framerate %d\n",
            vi_chn_attr.stFrameRate.s32SrcFrameRate, vi_chn_attr.stFrameRate.s32DstFrameRate);
    }
    vi_chn_attr.stSize.u32Width = width;
    vi_chn_attr.stSize.u32Height = height;
    vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
    vi_chn_attr.u32Depth = 0; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
    if (g_s32ChnCnt == 2)
        vi_chn_attr.u32Depth = 2;
    ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
    ret |= RK_MPI_VI_EnableChn(0, channelId);
    if (ret) {
        printf("ERROR: create VI error! ret=%d\n", ret);
        return ret;
    }

    return ret;
}

int test_vpss_init(int VpssChn, int width, int height) {
    printf("================================%s==================================\n",
           __func__);
    int s32Ret;
    VPSS_CHN_ATTR_S stVpssChnAttr;
    VPSS_GRP_ATTR_S stGrpVpssAttr;
    memset(&stVpssChnAttr, 0, sizeof(stVpssChnAttr));
    memset(&stGrpVpssAttr, 0, sizeof(stGrpVpssAttr));

    int s32Grp = 0;

    stGrpVpssAttr.u32MaxW = 4096;
    stGrpVpssAttr.u32MaxH = 4096;
    stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
    stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
    stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
    stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;

    stVpssChnAttr.enChnMode = VPSS_CHN_MODE_USER;
    stVpssChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
    stVpssChnAttr.enPixelFormat = RK_FMT_YUV420SP;
    stVpssChnAttr.u32FrameBufCnt = 8;
    if (g_s32FrameRate == 30) {
        stVpssChnAttr.stFrameRate.s32SrcFrameRate = -1;
        stVpssChnAttr.stFrameRate.s32DstFrameRate = -1;
    } else {
        stVpssChnAttr.stFrameRate.s32SrcFrameRate = 60;
        stVpssChnAttr.stFrameRate.s32DstFrameRate = 30;
    }
    stVpssChnAttr.u32Width = width;
    stVpssChnAttr.u32Height = height;
    stVpssChnAttr.enCompressMode = COMPRESS_MODE_NONE;

    s32Ret = RK_MPI_VPSS_CreateGrp(s32Grp, &stGrpVpssAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VPSS_SetChnAttr(s32Grp, VpssChn, &stVpssChnAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }
    s32Ret = RK_MPI_VPSS_EnableChn(s32Grp, VpssChn);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    if (g_s32ChnCnt == 2) {
        stVpssChnAttr.u32Width = width * 2;
        stVpssChnAttr.u32Height = height * 2;
        s32Ret = RK_MPI_VPSS_SetChnAttr(s32Grp, 1, &stVpssChnAttr);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
        s32Ret = RK_MPI_VPSS_EnableChn(s32Grp, 1);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
    }

    s32Ret = RK_MPI_VPSS_StartGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }
    return s32Ret;
}

static RK_CHAR optstr[] = "?::a::w:h:c:I:e:b:s:C:f:m:n:d:W:H:";
static void print_usage(const RK_CHAR *name) {
    printf("Usage example:\n");
    printf("\t%s -I 0 -w 1920 -h 1080 (rtsp://ip/live/0)\n", name);
    printf("\t-w | --width: VI width, Default:1920\n");
    printf("\t-h | --heght: VI height, Default:1080\n");
    printf("\t-a | --aiq: iq file path, Default:/etc/iqfiles\n");
    printf("\t-c | --frame_cnt: frame number of output, Default:-1\n");
#if defined RV1126_RV1109
    printf("\t-I | --chnid: camera 0 chn id, Default 0. "
            "0:rkisp_m_bypass, 1:rkispp_scale0, 2:rkispp_scale2, 2:rkispp_scale3\n");
#elif (defined RV1106_RV1103 || defined RV1103B)
    printf("\t-I | --chnid: camera 0 chn id, Default 0. "
            "0:rkisp_mainpath, 1:rkisp_selfpath, 2:rkisp_bypasspath\n");
#endif
    printf("\t-e | --encode: encode type, Default:h264, Value:h264, h265\n");
    printf("\t-b | --bitrate: set bitrate (Kbps), Default: 10Mbps\n");
    printf("\t-s | --slice_split: set slice split (0, 1), Default: 0\n");
    printf("\t-C | --chn cnt : set venc chn (1, 2), Default: 1\n");
    printf("\t-f | --fps : set venc encode frame rate, Default: 60\n");
    printf("\t-m | --rtsp mode : set rtsp send mode, Default: 1\n");
    printf("\t-n | --VIEntityName : default /dev/video0\n");
    printf("\t-d | --dst ip : dst ip for rtsp mode 2\n");
    printf("\t-W | --width: VPSS width, Default:1920\n");
    printf("\t-H | --heght: VPSS height, Default:1080\n");
}

int main(int argc, char *argv[]) {
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 u32Width = 1920;
    RK_U32 u32Height = 1080;
    RK_U32 u32ScaleWidth = 1920;
    RK_U32 u32ScaleHeight = 1080;
    // RK_CHAR *pOutPath = NULL;
    RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
    RK_CHAR *pCodecName = "H264";
    RK_S32 s32chnlId = 0;
    char *iq_dir = "/etc/iqfiles";
    int c;
    int ret = -1;

    while ((c = getopt(argc, argv, optstr)) != -1) {
        switch (c) {
        case 'a':
            if (optarg)
                iq_dir = optarg;
            break;
        case 'w':
            u32Width = atoi(optarg);
            break;
        case 'h':
            u32Height = atoi(optarg);
            break;
        case 'I':
            s32chnlId = atoi(optarg);
            break;
        case 'c':
            g_s32FrameCnt = atoi(optarg);
            break;
        case 'e':
            if (!strcmp(optarg, "h264")) {
                enCodecType = RK_VIDEO_ID_AVC;
                pCodecName = "H264";
            } else if (!strcmp(optarg, "h265")) {
                enCodecType = RK_VIDEO_ID_HEVC;
                pCodecName = "H265";
            } else {
                printf("ERROR: Invalid encoder type.\n");
                return -1;
            }
            break;
        case 'b':
            g_u32Bitrate = atoi(optarg);
            break;
        case 's':
            g_u32SliceSplit = atoi(optarg);
            break;
        case 'C':
            g_s32ChnCnt = atoi(optarg);
            break;
        case 'f':
            g_s32FrameRate = atoi(optarg);
            break;
        case 'm':
            g_rtsp_mode = atoi(optarg);
            break;
        case 'n':
            g_VIEntityName = optarg;
            break;
        case 'd':
            g_dst_ip = optarg;
            break;
        case 'W':
            u32ScaleWidth = atoi(optarg);
            break;
        case 'H':
            u32ScaleHeight = atoi(optarg);
            break;
        case '?':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    printf("#CodecName:%s\n", pCodecName);
    printf("#Resolution: %dx%d\n", u32Width, u32Height);
    printf("#ChannelIdx: %d\n\n", s32chnlId);
    printf("#Frame Count to save: %d\n", g_s32FrameCnt);

    signal(SIGINT, sigterm_handler);

    // init rtsp
    if (g_rtsp_mode == 1) {
        g_rtsplive = create_rtsp_demo(554);
        g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
        g_rtsp_session1 = rtsp_new_session(g_rtsplive, "/live/1");
        if (enCodecType == RK_VIDEO_ID_AVC) {
            rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
            rtsp_set_video(g_rtsp_session1, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
        } else if (enCodecType == RK_VIDEO_ID_HEVC) {
            rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
            rtsp_set_video(g_rtsp_session1, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
        } else {
            printf("not support other type\n");
            return -1;
        }
        rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
        rtsp_sync_video_ts(g_rtsp_session1, rtsp_get_reltime(), rtsp_get_ntptime());
    }

    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        RK_LOGE("rk mpi sys init fail!");
        goto __FAILED;
    }

    // venc  init
    test_venc_init(0, u32Width, u32Height,
                   enCodecType); // RK_VIDEO_ID_AVC RK_VIDEO_ID_HEVC

    // vi  init
    vi_dev_init();
    vi_chn_init(s32chnlId, u32Width, u32Height);

    MPP_CHN_S stSrcChn, stDestChn;
    // bind vi to venc

    if (g_s32ChnCnt == 2) {
        test_venc_init(1, u32ScaleWidth, u32ScaleHeight,
                enCodecType); // RK_VIDEO_ID_AVC RK_VIDEO_ID_HEVC
        test_vpss_init(0, u32ScaleWidth, u32ScaleHeight);

        // stSrcChn.enModId = RK_ID_VPSS;
        // stSrcChn.s32DevId = 0;
        // stSrcChn.s32ChnId = 1;
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = s32chnlId;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        printf("====RK_MPI_SYS_Bind vpss1 to venc0====\n");
        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);

        stSrcChn.enModId = RK_ID_VPSS;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = 0;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 1;
        printf("====RK_MPI_SYS_Bind vpss0 to venc1====\n");
        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);

    } else {
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = s32chnlId;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        printf("====RK_MPI_SYS_Bind vi0 to venc0====\n");
        s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("bind 0 ch venc failed");
            goto __FAILED;
        }
    }


    pthread_t main_thread;
    pthread_create(&main_thread, NULL, GetMediaBuffer0, NULL);

    pthread_t main_thread1;
    pthread_t main_thread2;
    if (g_s32ChnCnt == 2) {
        pthread_create(&main_thread1, NULL, GetMediaBuffer1, NULL);
        pthread_create(&main_thread2, NULL, GetVIBufferSendVPSS, NULL);
    }

    while (!quit) {
        usleep(500000);
    }
    pthread_join(main_thread, NULL);
    if (g_s32ChnCnt == 2) {
        pthread_join(main_thread1, NULL);
        pthread_join(main_thread2, NULL);
    }

    if (g_rtsp_mode == 1) {
        if (g_rtsplive)
            rtsp_del_demo(g_rtsplive);
    }

    if (g_s32ChnCnt == 2) {
        // stSrcChn.enModId = RK_ID_VPSS;
        // stSrcChn.s32DevId = 0;
        // stSrcChn.s32ChnId = 1;
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = s32chnlId;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        printf("====RK_MPI_SYS_UnBind vpss1 to venc0====\n");
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);

        stSrcChn.enModId = RK_ID_VPSS;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = 0;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 1;
        printf("====RK_MPI_SYS_UnBind vpss0 to venc1====\n");
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);

        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = s32chnlId;

        stDestChn.enModId = RK_ID_VPSS;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;

        printf("====RK_MPI_SYS_UnBind vi0 to vpss0====\n");
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);

        RK_MPI_VPSS_StopGrp(0);
        RK_MPI_VPSS_DestroyGrp(0);

        s32Ret = RK_MPI_VENC_StopRecvFrame(1);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }

        s32Ret = RK_MPI_VENC_DestroyChn(1);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
        }
    } else {
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = s32chnlId;

        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
        }
    }

    s32Ret = RK_MPI_VI_DisableChn(0, s32chnlId);
    RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

    s32Ret = RK_MPI_VENC_StopRecvFrame(0);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VENC_DestroyChn(0);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
    }

    s32Ret = RK_MPI_VI_DisableDev(0);
    RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);
    ret = 0;
__FAILED:
    RK_LOGE("test running exit:%d", s32Ret);
    RK_MPI_SYS_Exit();

    return ret;
}
