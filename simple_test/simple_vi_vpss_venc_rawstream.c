#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sys/prctl.h>

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
#include "rk_mpi_avs.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_ivs.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_rgn.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_tde.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vpss.h"
#include "rk_comm_vpss.h"

#define RK_MAX(a, b) ((a) > (b) ? (a) : (b))
#define RK_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
static RK_S32 g_s32FrameCnt = -1;

typedef struct {
	RK_U32 u32PipeId;
	RK_U32 u32ChnId;
	RK_U32 u32InWidth;
	RK_U32 u32InHeight;
	RK_U32 u32OutWidth;
	RK_U32 u32OutHeight;
	RK_U32 u32MaxWidth;
	RK_U32 u32MaxHeight;
	RK_BOOL bEnableWrap;
	RK_U32 u32WrapLine;
	RK_U32 u32Framerate;
	RK_U32 u32DstFramerate;
	VI_RAW_MEMORY_TYPE_E enMemMode;
	RK_U32 u32BufCnt;
	RK_U32 u32PipeMbCnt;
	RK_BOOL bMirror;
	RK_BOOL bFlip;
	FILE *fp;
	FILE *fp1;
	pthread_t tid;
} TEST_VI_CFG_S;

typedef struct {
	RK_U32 u32ChnId;
	RK_U32 u32Width;
	RK_U32 u32Height;
	RK_S32 s32FrameCnt;
	RK_CODEC_ID_E enCodecType;
	RK_BOOL bEnableWrap;
	RK_U32 u32WrapLine;
	RK_U32 u32Bitrate;
	RK_U32 u32Framerate;
	RK_U32 u32BufSize;
	FILE *fp;
	pthread_t tid;
} TEST_VENC_CFG_S;

typedef enum {
	TEST_OFFLINE_MODE = 0,
	TEST_ONLINE_MODE
} TEST_LINE_MODE;

typedef enum {
	TEST_VI_MODE_VI_ONLY = 0,
	TEST_VI_MODE_BIND_VPSS,
	TEST_VI_MODE_BIND_VPSS_BIND_VENC,
	TEST_VI_MODE_BIND_VENC,
} TEST_VI_MODE;

typedef struct rkVPSS_CFG_S {
	const char *dstFilePath;
	RK_S32 s32Grp;
	RK_S32 s32ChnId;
	RK_U32 u32VpssChnCnt;
	VPSS_GRP_ATTR_S stGrpVpssAttr;
	VPSS_CHN_ATTR_S stVpssChnAttr[VPSS_MAX_CHN_NUM];
	pthread_t tid;
	RK_BOOL bGrpCropEn;
	RK_BOOL bChnCropEn;
	RK_S32  s32GrpRotation;
	RK_S32  s32GrpMirror;
	RK_S32  s32GrpFlip;
	RK_S32  s32GrpCropRatio;
	RK_S32  s32ChnCropRatio;
	RK_S32  s32Rotation;
	RK_S32  s32RotationEx;
	RK_S32  s32Mirror;
	RK_S32  s32Flip;
	RK_U32  u32Width;
	RK_U32  u32Height;
	RK_BOOL bEnableWrap;
	RK_U32  u32WrapLine;
	RK_S32  s32DstCompressMode;
} TEST_VPSS_CFG_S;

typedef struct {
	RK_U32		  u32CamNum;
	TEST_LINE_MODE  firstMode;
	TEST_LINE_MODE  secondMode;
	TEST_VI_CFG_S   stViCtx[2];
	TEST_VPSS_CFG_S stVpssCtx[2];
	TEST_VENC_CFG_S stVencCtx[2];
	TEST_VI_MODE  viMode;
} TEST_CTX_S;

#define MAX_AIQ_CTX 2
static rk_aiq_sys_ctx_t *g_aiq_ctx[MAX_AIQ_CTX];
rk_aiq_working_mode_t g_WDRMode[MAX_AIQ_CTX];
static atomic_int g_sof_cnt = 0;
static atomic_bool g_should_quit = false;

static TEST_CTX_S stTestCtx = {0};
static bool quit1 = false;
static bool quit2 = false;
static RK_U32 u32CamNum = 1;

static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit1 = true;
	quit2 = true;
}

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

static int vi_pipe_init(TEST_VI_CFG_S *pViCfg) {
	int ret;
	VI_PIPE_ATTR_S stPipeAttr;

	memset(&stPipeAttr, 0, sizeof(stPipeAttr));
	stPipeAttr.u32MaxW = pViCfg->u32InWidth;
	stPipeAttr.u32MaxH = pViCfg->u32InHeight;
	stPipeAttr.enPixFmt = RK_FMT_RGB_BAYER_SBGGR_10BPP;
	stPipeAttr.enMemMode = pViCfg->enMemMode;
	ret = RK_MPI_VI_CreatePipe(pViCfg->u32PipeId, &stPipeAttr);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VI_CreatePipe[%d] failed: %x\n", pViCfg->u32PipeId, ret);
		return -1;
	}

	return 0;
}


//根据不同的channel来选择不同的vi节点
static int vi_dev_init(int devId) {
	printf("%s\n", __func__);
	int ret = 0;
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
			printf("RK_MPI_VI_SetDevAttr[%d] failed: %x\n", devId, ret);
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
			printf("RK_MPI_VI_EnableDev[%d] %x\n", devId, ret);
			return -1;
		}

		// 1-3.bind dev/pipe
		stBindPipe.u32Num = 1;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevBindPipe[%d] %x\n", devId, ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

static int vi_chn_init(TEST_VI_CFG_S *pViCfg) {
	int ret = 0;
	VI_CHN_ATTR_S stViChnAttr;
	VI_CHN_BUF_WRAP_S stViWrap;

	printf("enter vi_chn_init[%d, %d]\n", pViCfg->u32PipeId, pViCfg->u32ChnId);

	memset(&stViChnAttr, 0, sizeof(VI_CHN_ATTR_S));
	stViChnAttr.stIspOpt.u32BufCount = pViCfg->u32BufCnt;
	stViChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	stViChnAttr.stSize.u32Width = pViCfg->u32OutWidth;
	stViChnAttr.stSize.u32Height = pViCfg->u32OutHeight;
	stViChnAttr.enPixelFormat = RK_FMT_YUV420SP;
	stViChnAttr.stFrameRate.s32SrcFrameRate = pViCfg->u32Framerate;
	stViChnAttr.stFrameRate.s32DstFrameRate = pViCfg->u32DstFramerate;
	stViChnAttr.bMirror = pViCfg->bMirror;
	stViChnAttr.bFlip = pViCfg->bFlip;

	stViChnAttr.stIspOpt.stMaxSize.u32Width = pViCfg->u32MaxWidth;
	stViChnAttr.stIspOpt.stMaxSize.u32Height = pViCfg->u32MaxHeight;
	stViChnAttr.u32Depth = 1; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount

	if (stTestCtx.u32CamNum > 1 && pViCfg->bEnableWrap) {
		stViChnAttr.stShareBufChn.enModId = RK_ID_VI;
		stViChnAttr.stShareBufChn.s32DevId = stTestCtx.stViCtx[0].u32PipeId;
		stViChnAttr.stShareBufChn.s32ChnId = stTestCtx.stViCtx[0].u32ChnId;
		stViChnAttr.enAllocBufType = pViCfg->u32PipeId? VI_ALLOC_BUF_TYPE_CHN_SHARE : VI_ALLOC_BUF_TYPE_INTERNAL;
	}

	printf("vi[%d, %d] enAllocBufType: %d, stShareBufChn.s32ChnId: %d\n",
					pViCfg->u32PipeId, pViCfg->u32ChnId, stViChnAttr.enAllocBufType, stViChnAttr.stShareBufChn.s32ChnId);

	ret = RK_MPI_VI_SetChnAttr(pViCfg->u32PipeId, pViCfg->u32ChnId, &stViChnAttr);
	if (ret) {
		printf("ERROR: VI set channel attribute fail, ret=%d\n", ret);
		return ret;
	}

	// set wrap mode attr
	memset(&stViWrap, 0, sizeof(VI_CHN_BUF_WRAP_S));
	if (pViCfg->bEnableWrap) {
		if ((stTestCtx.u32CamNum == 1) && (pViCfg->u32WrapLine < 64 || pViCfg->u32WrapLine > pViCfg->u32OutHeight)) {
			printf("wrap mode buffer line must between [64, %d], u32WrapLine: %d\n", pViCfg->u32OutHeight, pViCfg->u32WrapLine);
			return -1;
		}

		stViWrap.bEnable = RK_TRUE;
		stViWrap.u32BufLine = pViCfg->u32WrapLine;
		stViWrap.u32WrapBufferSize = stViWrap.u32BufLine * pViCfg->u32OutWidth * 3 / 2; // nv12 (w * wrapLine *3 / 2)
		printf("set channel: %d wrap line: %d, wrapBuffSize = %d\n", pViCfg->u32ChnId, pViCfg->u32WrapLine, stViWrap.u32WrapBufferSize);
		ret = RK_MPI_VI_SetChnWrapBufAttr(pViCfg->u32PipeId, pViCfg->u32ChnId, &stViWrap);
		if (ret) {
			printf("ERROR: RK_MPI_VI_SetChnWrapBufAttr failed: %x\n", ret);
			return ret;
		}
	}

	ret = RK_MPI_VI_EnableChn(pViCfg->u32PipeId, pViCfg->u32ChnId);
	if (ret) {
		printf("ERROR: RK_MPI_VI_EnableChn failed: %x\n", ret);
		return ret;
	}

	printf("vi dev:%d channle:%d enable success\n", pViCfg->u32PipeId, pViCfg->u32ChnId);
	return ret;
}

static RK_S32 venc_chn_init(TEST_VENC_CFG_S *pVencCfg) {
	int ret;
	VENC_CHN_ATTR_S stVencChnAttr;
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
	VENC_RC_MODE_E enRcMode = VENC_RC_MODE_BUTT;

	memset(&stVencChnAttr, 0, sizeof(VENC_CHN_ATTR_S));

	RK_U32 u32GopSize = 30; //pVencCfg->u32Framerate;

	stVencChnAttr.stVencAttr.enType = pVencCfg->enCodecType;
	stVencChnAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;

	if (pVencCfg->enCodecType == RK_VIDEO_ID_AVC) {
		enRcMode = VENC_RC_MODE_H264CBR;
		stVencChnAttr.stRcAttr.stH264Cbr.u32BitRate = pVencCfg->u32Bitrate;
		stVencChnAttr.stRcAttr.stH264Cbr.u32Gop = u32GopSize;
		stVencChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
		stVencChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = pVencCfg->u32Framerate;
		stVencChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
		stVencChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = pVencCfg->u32Framerate;
	} else if (pVencCfg->enCodecType == RK_VIDEO_ID_HEVC) {
		enRcMode = VENC_RC_MODE_H265CBR;
		stVencChnAttr.stRcAttr.stH265Cbr.u32BitRate = pVencCfg->u32Bitrate;
		stVencChnAttr.stRcAttr.stH265Cbr.u32Gop = u32GopSize;
		stVencChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
		stVencChnAttr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = pVencCfg->u32Framerate;
		stVencChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
		stVencChnAttr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = pVencCfg->u32Framerate;
	} else if (pVencCfg->enCodecType == RK_VIDEO_ID_MJPEG) {
		enRcMode = VENC_RC_MODE_MJPEGCBR;
		stVencChnAttr.stRcAttr.stMjpegCbr.u32BitRate = pVencCfg->u32Bitrate;
		stVencChnAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateDen = 1;
		stVencChnAttr.stRcAttr.stMjpegCbr.fr32DstFrameRateNum = pVencCfg->u32Framerate;
		stVencChnAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateDen = 1;
		stVencChnAttr.stRcAttr.stMjpegCbr.u32SrcFrameRateNum = pVencCfg->u32Framerate;
	}

	stVencChnAttr.stRcAttr.enRcMode = enRcMode;
	stVencChnAttr.stVencAttr.u32MaxPicWidth = pVencCfg->u32Width;
	stVencChnAttr.stVencAttr.u32MaxPicHeight = pVencCfg->u32Height;
	stVencChnAttr.stVencAttr.u32PicWidth = pVencCfg->u32Width;
	stVencChnAttr.stVencAttr.u32PicHeight = pVencCfg->u32Height;
	stVencChnAttr.stVencAttr.u32VirWidth = pVencCfg->u32Width;
	stVencChnAttr.stVencAttr.u32VirHeight = pVencCfg->u32Height;
	stVencChnAttr.stVencAttr.u32StreamBufCnt = 5;
	stVencChnAttr.stVencAttr.u32BufSize = pVencCfg->u32BufSize;
	ret = RK_MPI_VENC_CreateChn(pVencCfg->u32ChnId, &stVencChnAttr);
	if (ret) {
		printf("RK_MPI_VENC_CreateChn[%d] fialed: %x\n", pVencCfg->u32ChnId, ret);
		return -1;
	}

	if (pVencCfg->bEnableWrap) {
		memset(&stVencChnBufWrap, 0, sizeof(stVencChnBufWrap));
		if ((stTestCtx.u32CamNum == 1) && (pVencCfg->u32WrapLine < 64 || pVencCfg->u32WrapLine > pVencCfg->u32Height)) {
			printf("wrap mode buffer line must between [64, %d], u32WrapLine: %d\n", pVencCfg->u32Height, pVencCfg->u32WrapLine);
			RK_MPI_VENC_DestroyChn(pVencCfg->u32ChnId);
			return -1;
		}

		stVencChnBufWrap.bEnable = RK_TRUE;
		stVencChnBufWrap.u32BufLine = pVencCfg->u32WrapLine;
		printf("set venc channel wrap line: %d\n", stVencChnBufWrap.u32BufLine);

		ret = RK_MPI_VENC_SetChnBufWrapAttr(pVencCfg->u32ChnId, &stVencChnBufWrap);
		if (ret)
			printf("RK_MPI_VENC_SetChnBufWrapAttr[%d] failed: %x\n", pVencCfg->u32ChnId, ret);
	}

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = pVencCfg->s32FrameCnt;
	ret = RK_MPI_VENC_StartRecvFrame(pVencCfg->u32ChnId, &stRecvParam);
	if (ret)
		printf("RK_MPI_VENC_StartRecvFrame[%d] failed: %x\n", pVencCfg->u32ChnId, ret);

	return RK_SUCCESS;
}

static void *send_raw_frame(void *arg) {
	RK_S32 ret = RK_SUCCESS;
	RK_S32 waitTime = 3000;
	RK_S32 loopCount = 0;
	VIDEO_FRAME_INFO_S stViFrame = {0};
	PIC_BUF_ATTR_S pstBufAttr = {0};
	MB_PIC_CAL_S pstPicCal = {0};
	MB_POOL_CONFIG_S stMbPoolCfg = {0};
	MB_POOL mbPool = MB_INVALID_POOLID;
	TEST_VI_CFG_S *pstViCfg = (TEST_VI_CFG_S *)arg;
	RK_U64 timeInterval = 0;
	if (pstViCfg->u32Framerate != -1)
		timeInterval = 1000000 / pstViCfg->u32Framerate;
	else
		timeInterval = 0;
	memset(&stViFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
	memset(&pstBufAttr, 0, sizeof(PIC_BUF_ATTR_S));
	memset(&pstPicCal, 0, sizeof(MB_PIC_CAL_S));
	memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
	prctl(PR_SET_NAME, "send_raw");
	pstBufAttr.u32Width = pstViCfg->u32InWidth;
	pstBufAttr.u32Height = pstViCfg->u32InHeight;
	pstBufAttr.enCompMode = COMPRESS_MODE_NONE;
	pstBufAttr.enPixelFormat = RK_FMT_RGB_BAYER_SBGGR_10BPP;
	ret = RK_MPI_CAL_COMM_GetPicBufferSize(&pstBufAttr, &pstPicCal);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_CAL_TDE_GetPicBufferSize[%d, %d] failure: %x\n", pstViCfg->u32PipeId, pstViCfg->u32ChnId, ret);
		return RK_NULL;
	}

	printf("per buff size: %d\n", pstPicCal.u32MBSize);
	stMbPoolCfg.u64MBSize = pstPicCal.u32MBSize;
	stMbPoolCfg.u32MBCnt = pstViCfg->u32PipeMbCnt;
	stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
	stMbPoolCfg.bPreAlloc = RK_TRUE;
	mbPool = RK_MPI_MB_CreatePool(&stMbPoolCfg);
	if (mbPool == MB_INVALID_POOLID)
		printf("RK_MPI_MB_CreatePool[%d, %d] failure\n", pstViCfg->u32PipeId, pstViCfg->u32ChnId);

	while (1) {
		if (u32CamNum == 1) {
			if (quit1)
				break;
		} else {
			if (pstViCfg->u32PipeId == 1) {
				if (quit2)
					break;
			} else {
				if (quit1)
					break;
			}
		}

		stViFrame.stVFrame.pMbBlk = RK_MPI_MB_GetMB(mbPool, pstPicCal.u32MBSize, RK_TRUE);
		if (stViFrame.stVFrame.pMbBlk == MB_INVALID_HANDLE) {
			usleep(1000);
			continue;
		}

		stViFrame.stVFrame.u32Width = pstBufAttr.u32Width;
		stViFrame.stVFrame.u32Height = pstBufAttr.u32Height;
		stViFrame.stVFrame.u32VirWidth = pstPicCal.u32VirWidth;
		stViFrame.stVFrame.u32VirHeight = pstPicCal.u32VirHeight;
		stViFrame.stVFrame.u32TimeRef = loopCount;
		stViFrame.stVFrame.u64PTS = loopCount * timeInterval;
		stViFrame.stVFrame.enPixelFormat = RK_FMT_RGB_BAYER_SBGGR_10BPP;
		stViFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;

		if (loopCount % 2 == 0) {
			if (pstViCfg->fp) {
				ret = fread(RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk), 1, pstPicCal.u32MBSize, pstViCfg->fp);
				ret = fseek(pstViCfg->fp, 0, SEEK_SET);
				if (ret)
					printf("fseek failed[%d] errno[%d, %s]", ret, errno, strerror(errno));
			} else {
				memset(RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk), 0, pstPicCal.u32MBSize);
			}
		} else {
			if (pstViCfg->fp1) {
				ret = fread(RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk), 1, pstPicCal.u32MBSize, pstViCfg->fp1);
				ret = fseek(pstViCfg->fp1, 0, SEEK_SET);
				if (ret)
					printf("fseek failed[%d] errno[%d, %s]", ret, errno, strerror(errno));
			} else {
				memset(RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk), 0, pstPicCal.u32MBSize);
			}
		}

		RK_MPI_SYS_MmzFlushCache(stViFrame.stVFrame.pMbBlk, RK_FALSE);
		ret = RK_MPI_VI_PipeSendFrame(pstViCfg->u32PipeId, &stViFrame, waitTime);
		if (ret != RK_SUCCESS)
			printf("RK_MPI_VI_PipeSendFrame[%d, %d] failure:%x\n", pstViCfg->u32PipeId, pstViCfg->u32ChnId, ret);
		else
			printf("+++ %d: send pipe success[%d, %d], pts: %lu, loop fp:%d quit: %d %d\n", loopCount, pstViCfg->u32PipeId,
				pstViCfg->u32ChnId, stViFrame.stVFrame.u64PTS, loopCount % 2, quit1, quit2);

		loopCount++;
		RK_MPI_MB_ReleaseMB(stViFrame.stVFrame.pMbBlk);
		stViFrame.stVFrame.pMbBlk = RK_NULL;
		if (timeInterval)
			usleep(timeInterval / 2);
	}

	RK_MPI_MB_DestroyPool(mbPool);
	printf("Exit send raw frame thread[%d, %d]\n", pstViCfg->u32PipeId, pstViCfg->u32ChnId);
	return NULL;
}

static void *get_vi_frame(void *arg) {
	printf("========%s========\n", __func__);
	int loopCount = 0;
	int s32Ret;
	RK_S32 waitTime = 1000;
	int pipeId = 0;
	int channelId = 0;
	pipeId = *(int *)arg;
	VIDEO_FRAME_INFO_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	FILE *fp = NULL;
	char inputPath[256] = {0};
	bool (*quit) = &quit1;
	while (!(*quit)) {
		s32Ret = RK_MPI_VI_GetChnFrame(pipeId, channelId, &stViFrame, waitTime);
		if (s32Ret == RK_SUCCESS) {
			RK_U64 nowUs = TEST_COMM_GetNowUs();
			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
			if (!loopCount) {
				sprintf(inputPath, "mnt/sdcard/vi_%d.bin", pipeId);
				fp = fopen(inputPath, "wb");
				if (fp) {
					fwrite(data, 1, stViFrame.stVFrame.u64PrivateData, fp);
					fflush(fp);
					fclose(fp);
				}
			}
			RK_LOGD(
				"RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%lld ms len=%llu",
				data, loopCount, stViFrame.stVFrame.u32TimeRef,
				stViFrame.stVFrame.u64PTS / 1000, stViFrame.stVFrame.u64PrivateData);
			// 6.get the channel status
			s32Ret = RK_MPI_VI_QueryChnStatus(pipeId, channelId, &stChnStatus);
			RK_LOGD("RK_MPI_VI_QueryChnStatus ret %x, w:%d,h:%d,enable:%d,"
					"current frame id:%d,input lost:%d,output lost:%d,"
					"framerate:%d,vbfail:%d delay=%lldus",
					s32Ret, stChnStatus.stSize.u32Width, stChnStatus.stSize.u32Height,
					stChnStatus.bEnable, stChnStatus.u32CurFrameID,
					stChnStatus.u32InputLostFrame, stChnStatus.u32OutputLostFrame,
					stChnStatus.u32FrameRate, stChnStatus.u32VbFail,
					nowUs - stViFrame.stVFrame.u64PTS);

			// 7.release the frame
			s32Ret = RK_MPI_VI_ReleaseChnFrame(pipeId, channelId, &stViFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
			}
			loopCount++;
		} else {
			RK_LOGE("RK_MPI_VI_GetChnFrame timeout %x", s32Ret);
		}

		if ((g_s32FrameCnt >= 0) && (loopCount > g_s32FrameCnt)) {
			(*quit) = true;
			break;
		}
	}

	return NULL;
}

static void *get_venc_stream(void *arg) {
	RK_S32 s32Ret;
	RK_S32 loopCount = 0;
	RK_S32 totalCount = 0;
	void *pData = RK_NULL;
	VENC_PACK_S stPack;
	VENC_STREAM_S stFrame;
	RK_U64 u64prepts = 0;
	prctl(PR_SET_NAME, "get_venc_stream");
	TEST_VENC_CFG_S *pstVencCfg = (TEST_VENC_CFG_S *)arg;
	bool *quit = pstVencCfg->u32ChnId == 0 ? &quit1 : &quit2;
	stFrame.pstPack = &stPack;
	while (!(*quit)) {
		s32Ret = RK_MPI_VENC_GetStream(pstVencCfg->u32ChnId, &stFrame, 5000);
		if (s32Ret == RK_SUCCESS) {
			if (pstVencCfg->fp) {
				pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				fwrite(pData, 1, stFrame.pstPack->u32Len, pstVencCfg->fp);
				fflush(pstVencCfg->fp);
			}
			RK_U64 nowUs = TEST_COMM_GetNowUs();

			printf("get--- %d, chn:%d, seq:%d, len:%d, pts=%ld, diff=%ld, delay=%ldus\n",
				loopCount, pstVencCfg->u32ChnId, stFrame.u32Seq, stFrame.pstPack->u32Len,
				stFrame.pstPack->u64PTS, stFrame.pstPack->u64PTS - u64prepts, nowUs - stFrame.pstPack->u64PTS);

			u64prepts = stFrame.pstPack->u64PTS;
			s32Ret = RK_MPI_VENC_ReleaseStream(pstVencCfg->u32ChnId, &stFrame);
			if (s32Ret != RK_SUCCESS)
				printf("RK_MPI_VENC_ReleaseStream[%d] failed: %x\n", pstVencCfg->u32ChnId, s32Ret);

			loopCount++;
			totalCount = 0;
		} else {
			printf("RK_MPI_VENC_GetStream[%d] timeout: %x\n", pstVencCfg->u32ChnId, s32Ret);
		}

		if ((g_s32FrameCnt >= 0) && (loopCount >= g_s32FrameCnt)) {
			(*quit) = true;
			break;
		}

		totalCount++;
		if (totalCount > 30) {
			(*quit) = true;
			break;
		}
	}

	printf("Exit get venc[%d] stream thread, quit %d %d %d\n", pstVencCfg->u32ChnId, quit1, quit2, (*quit));
	system("cat /proc/vcodec/enc/venc_info | grep -A 2 RealFps");
	return NULL;
}

RK_S32 TEST_VPSS_ChnSetZoom(VPSS_GRP VpssGrp, VPSS_CHN VpssChn, RK_U32 u32Zoom, RK_BOOL bEnable) {
	RK_S32 s32Ret = RK_SUCCESS;
	VPSS_CROP_INFO_S stCropInfo;

	s32Ret = RK_MPI_VPSS_GetChnCrop(VpssGrp, VpssChn, &stCropInfo);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_GetChnCrop failed with %#x!", s32Ret);
		return s32Ret;
	}

	stCropInfo.bEnable = bEnable;
	stCropInfo.enCropCoordinate = VPSS_CROP_RATIO_COOR;
	stCropInfo.stCropRect.s32X = 500 - u32Zoom / 2;
	stCropInfo.stCropRect.s32Y = 500 - u32Zoom / 2;
	stCropInfo.stCropRect.u32Width = u32Zoom;
	stCropInfo.stCropRect.u32Height = u32Zoom;
	s32Ret = RK_MPI_VPSS_SetChnCrop(VpssGrp, VpssChn, &stCropInfo);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_SetChnCrop failed with %#x!", s32Ret);
		return s32Ret;
	}

	return s32Ret;
}

RK_S32 TEST_VPSS_SetChnRotation(VPSS_GRP VpssGrp, VPSS_CHN VpssChn, ROTATION_E enRotation) {
	RK_S32 s32Ret = RK_SUCCESS;
	ROTATION_E rotation = ROTATION_0;

	s32Ret = RK_MPI_VPSS_GetChnRotation(VpssGrp, VpssChn, &rotation);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_GetChnRotation failed with %#x!", s32Ret);
		return s32Ret;
	}
	s32Ret = RK_MPI_VPSS_SetChnRotation(VpssGrp, VpssChn, enRotation);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_SetChnRotation failed with %#x!", s32Ret);
		return s32Ret;
	}

	return s32Ret;
}

RK_S32 TEST_VPSS_SetChnRotationEx(VPSS_GRP VpssGrp, VPSS_CHN VpssChn, RK_U32 u32Angle) {
	RK_S32 s32Ret = RK_SUCCESS;
	VPSS_ROTATION_EX_ATTR_S stRotationEx;

	if (u32Angle == 0) {
		return s32Ret;
	}

	stRotationEx.bEnable = RK_TRUE;
	stRotationEx.stRotationEx.u32Angle = u32Angle;
	s32Ret = RK_MPI_VPSS_SetChnRotationEx(VpssGrp, VpssChn, &stRotationEx);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	s32Ret = RK_MPI_VPSS_GetChnRotationEx(VpssGrp, VpssChn, &stRotationEx);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	if (stRotationEx.bEnable != RK_TRUE
			|| stRotationEx.stRotationEx.u32Angle != u32Angle) {
		s32Ret = RK_FAILURE;
		return s32Ret;
	}

	return s32Ret;
}

RK_S32 TEST_VPSS_GrpSetZoom(VPSS_GRP VpssGrp, RK_U32 u32Zoom, RK_BOOL bEnable) {
	RK_S32 s32Ret = RK_SUCCESS;
	VPSS_CROP_INFO_S stCropInfo;

	s32Ret = RK_MPI_VPSS_GetGrpCrop(VpssGrp, &stCropInfo);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_GetGrpCrop failed with %#x!", s32Ret);
		return s32Ret;
	}

	stCropInfo.bEnable = bEnable;
	stCropInfo.enCropCoordinate = VPSS_CROP_RATIO_COOR;
	stCropInfo.stCropRect.s32X = 500 - u32Zoom / 2;
	stCropInfo.stCropRect.s32Y = 500 - u32Zoom / 2;
	stCropInfo.stCropRect.u32Width = u32Zoom;
	stCropInfo.stCropRect.u32Height = u32Zoom;
	s32Ret = RK_MPI_VPSS_SetGrpCrop(VpssGrp, &stCropInfo);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_SetGrpCrop failed with %#x!", s32Ret);
		return s32Ret;
	}

	return s32Ret;
}

static RK_S32 vpss_chn_init(TEST_VPSS_CFG_S *pstVpssCfg) {
	RK_S32 s32Ret = RK_SUCCESS;
	VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = { VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3 };
	RK_S32 i = 0;
	/* vpss */
	pstVpssCfg->u32VpssChnCnt = 1;
	pstVpssCfg->stGrpVpssAttr.u32MaxW = 4096;
	pstVpssCfg->stGrpVpssAttr.u32MaxH = 4096;
	pstVpssCfg->stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	pstVpssCfg->stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
	pstVpssCfg->stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
	pstVpssCfg->stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;
	VIDEO_PROC_DEV_TYPE_E enVProcDev = VIDEO_PROC_DEV_RGA;
#ifdef RV1126B
	pstVpssCfg->stGrpVpssAttr.enVProcDev = VIDEO_PROC_DEV_VPSS;
	enVProcDev = VIDEO_PROC_DEV_VPSS;
#endif

	for (i = 0; i < VPSS_MAX_CHN_NUM; i ++) {
		pstVpssCfg->stVpssChnAttr[i].enChnMode = VPSS_CHN_MODE_USER;
		pstVpssCfg->stVpssChnAttr[i].u32FrameBufCnt = 4;
		pstVpssCfg->stVpssChnAttr[i].enDynamicRange = DYNAMIC_RANGE_SDR8;
		pstVpssCfg->stVpssChnAttr[i].enPixelFormat = RK_FMT_YUV420SP;
		pstVpssCfg->stVpssChnAttr[i].stFrameRate.s32SrcFrameRate = -1;
		pstVpssCfg->stVpssChnAttr[i].stFrameRate.s32DstFrameRate = -1;
		pstVpssCfg->stVpssChnAttr[i].u32Width = pstVpssCfg->u32Width;
		pstVpssCfg->stVpssChnAttr[i].u32Height = pstVpssCfg->u32Height;;
		pstVpssCfg->stVpssChnAttr[i].enCompressMode = pstVpssCfg->s32DstCompressMode;
		pstVpssCfg->stVpssChnAttr[i].u32Depth = 1;
		pstVpssCfg->stVpssChnAttr[i].bFlip = (RK_BOOL)pstVpssCfg->s32Flip;
		pstVpssCfg->stVpssChnAttr[i].bMirror = (RK_BOOL)pstVpssCfg->s32Mirror;
	}

	s32Ret = RK_MPI_VPSS_CreateGrp(pstVpssCfg->s32Grp, &pstVpssCfg->stGrpVpssAttr);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

#ifdef RV1126B
	VPSS_CHN_BUF_WRAP_S wrapSet = {0};
	wrapSet.bEnable = pstVpssCfg->bEnableWrap;
	wrapSet.u32BufLine = pstVpssCfg->u32WrapLine;
#endif

	for (i = pstVpssCfg->s32ChnId; i < pstVpssCfg->s32ChnId + 1; i++) {
		s32Ret = RK_MPI_VPSS_SetChnAttr(pstVpssCfg->s32Grp, VpssChn[i], &pstVpssCfg->stVpssChnAttr[i]);
		if (s32Ret != RK_SUCCESS) {
			return s32Ret;
		}

#ifdef RV1126B
		s32Ret = RK_MPI_VPSS_SetChnWrapBufAttr((VPSS_GRP)pstVpssCfg->s32Grp, (VPSS_GRP)pstVpssCfg->s32ChnId, (const VPSS_CHN_BUF_WRAP_S *)&wrapSet);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VPSS_SetChnWrapBufAttr failed with %#x", s32Ret);
			return s32Ret;
		}

		if (pstVpssCfg->s32Grp == 1) {
			MPP_CHN_S stShareBufChn = {0};
			stShareBufChn.enModId = RK_ID_VPSS;
			stShareBufChn.s32DevId = 0;
			stShareBufChn.s32ChnId = 0;

			RK_MPI_VPSS_EnableBufferShare((VPSS_GRP)pstVpssCfg->s32Grp,
			(VPSS_GRP)pstVpssCfg->s32ChnId, stShareBufChn);
		}
#endif

		s32Ret = RK_MPI_VPSS_EnableChn(pstVpssCfg->s32Grp, VpssChn[i]);
		if (s32Ret != RK_SUCCESS) {
			return s32Ret;
		}
	}

	s32Ret = RK_MPI_VPSS_EnableBackupFrame(pstVpssCfg->s32Grp);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_EnableBackupFrame failed with %#x", s32Ret);
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_StartGrp(pstVpssCfg->s32Grp);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_StartGrp failed with %#x", s32Ret);
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_ResetGrp(pstVpssCfg->s32Grp);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_ResetGrp failed with %#x", s32Ret);
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_SetVProcDev(pstVpssCfg->s32Grp, enVProcDev);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_SetVProcDev(grp:%d) failed with %#x!", pstVpssCfg->s32Grp, s32Ret);
		return s32Ret;
	}

	s32Ret = TEST_VPSS_GrpSetZoom(pstVpssCfg->s32Grp, pstVpssCfg->s32GrpCropRatio, pstVpssCfg->bGrpCropEn);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

#ifdef RV1126B
	s32Ret = RK_MPI_VPSS_SetGrpRotation(pstVpssCfg->s32Grp, (ROTATION_E)pstVpssCfg->s32GrpRotation);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_SetGrpMirror(pstVpssCfg->s32Grp, (RK_BOOL)pstVpssCfg->s32GrpMirror, (RK_BOOL)pstVpssCfg->s32GrpFlip);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VPSS_SetGrpMirror(grp:%d) failed with %#x! mirror %d, flip %d.",
			pstVpssCfg->s32Grp, s32Ret, pstVpssCfg->s32GrpMirror, pstVpssCfg->s32GrpFlip);
	}
#endif

	for (RK_S32 chnIndex = 0; chnIndex < pstVpssCfg->u32VpssChnCnt; chnIndex++) {
		s32Ret = TEST_VPSS_ChnSetZoom(pstVpssCfg->s32Grp, chnIndex, pstVpssCfg->s32ChnCropRatio, pstVpssCfg->bChnCropEn);
		if (s32Ret != RK_SUCCESS) {
			return s32Ret;
		}
		s32Ret = TEST_VPSS_SetChnRotation(pstVpssCfg->s32Grp, chnIndex, (ROTATION_E)pstVpssCfg->s32Rotation);
		if (s32Ret != RK_SUCCESS) {
			return s32Ret;
		}
		s32Ret = TEST_VPSS_SetChnRotationEx(pstVpssCfg->s32Grp, chnIndex, pstVpssCfg->s32RotationEx);
		if (s32Ret != RK_SUCCESS) {
			return s32Ret;
		}
	}

	return  RK_SUCCESS;
}

static RK_S32 vpss_chn_deinit(RK_S32 s32Grp, RK_S32 s32OutChnNum) {
	RK_S32 s32Ret = RK_SUCCESS;
	VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = { VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3 };

	s32Ret = RK_MPI_VPSS_StopGrp(s32Grp);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	for (RK_S32 i = 0; i < s32OutChnNum; i++) {
		s32Ret = RK_MPI_VPSS_DisableChn(s32Grp, VpssChn[i]);
		if (s32Ret != RK_SUCCESS) {
			return s32Ret;
		}
	}

	s32Ret = RK_MPI_VPSS_DisableBackupFrame(s32Grp);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_DestroyGrp(s32Grp);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	return  RK_SUCCESS;
}

RK_S32 TEST_COMM_FileWriteOneFrame(const char *pFileName, VIDEO_FRAME_INFO_S *pstVideoFrame) {
	RK_S32 s32Ret = RK_SUCCESS;
	FILE *fp = RK_NULL;
	PIC_BUF_ATTR_S stPicBufAttr;
	MB_PIC_CAL_S stMbPicCalResult;

	if (!pFileName || !pstVideoFrame) {
		return RK_FAILURE;
	}

	stPicBufAttr.u32Width = pstVideoFrame->stVFrame.u32VirWidth;
	stPicBufAttr.u32Height = pstVideoFrame->stVFrame.u32VirHeight;
	stPicBufAttr.enPixelFormat = pstVideoFrame->stVFrame.enPixelFormat;
	stPicBufAttr.enCompMode = pstVideoFrame->stVFrame.enCompressMode;
	s32Ret = RK_MPI_CAL_VGS_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_CAL_VGS_GetPicBufferSize failed. err=0x%x", s32Ret);
		return s32Ret;
	}

	RK_MPI_SYS_MmzFlushCache(pstVideoFrame->stVFrame.pMbBlk, RK_TRUE);

	fp = fopen(pFileName, "wb");
	if (fp == RK_NULL) {
		RK_LOGE("open %s failed, error: %s", pFileName, strerror(errno));
		s32Ret = RK_FAILURE;
		goto __FAILED;
	}

	fwrite(RK_MPI_MB_Handle2VirAddr(pstVideoFrame->stVFrame.pMbBlk),
			1, stMbPicCalResult.u32MBSize, fp);
	fflush(fp);

__FAILED:
	if (fp) {
		fclose(fp);
	}

	return RK_SUCCESS;
}

static void *get_vpss_frame(void *arg) {
	RK_S32 s32Ret;
	RK_S32 loopCount = 0;
	prctl(PR_SET_NAME, "get_vpss_frame");
	TEST_VPSS_CFG_S *pstVpssCfg = (TEST_VPSS_CFG_S *)arg;
	VIDEO_FRAME_INFO_S stChnFrameInfos = {0};
	VIDEO_FRAME_INFO_S *pstVideoFrame = &stChnFrameInfos;
	char cWritePath[128] = {0};
	bool *quit = &quit1;
	while (!(*quit)) {
		s32Ret = RK_MPI_VPSS_GetChnFrame(0, 0, pstVideoFrame, -1);
		if (s32Ret != RK_SUCCESS) {
			return RK_SUCCESS;
		} else {
			loopCount++;
			RK_LOGI("vpss[%d] GetChnFrame[%d] ok", pstVpssCfg->s32ChnId, loopCount);
		}

		snprintf(cWritePath, sizeof(cWritePath), "/mnt/sdcard/vpss/chn_out_%dx%d_%d_%d.bin",
					stChnFrameInfos.stVFrame.u32VirWidth,
					stChnFrameInfos.stVFrame.u32VirHeight, pstVpssCfg->s32ChnId, loopCount);
		s32Ret = TEST_COMM_FileWriteOneFrame(cWritePath, &(stChnFrameInfos));
		if (s32Ret != RK_SUCCESS) {
			
		}

		s32Ret = RK_MPI_VPSS_ReleaseChnFrame(0, pstVpssCfg->s32ChnId, &(stChnFrameInfos));
		if (s32Ret != RK_SUCCESS) {
			break;
		}

		if ((g_s32FrameCnt >= 0) && (loopCount > g_s32FrameCnt)) {
			(*quit) = true;
			break;
		}
	}

	return NULL;
}

static RK_CHAR optstr[] = "?::d:n:c:I:o:w:W:l:L:d:m:b:f:s:v:F:r:R:t:p:P:y:Y:q:Q:e:E:k:K:N:S:D:x:X:O:";
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\tNOTE: Only RV1106 and RV1103 support this demo.\n");
	printf("\t%s -I 0 --in_width 1920 --in_height 1080 --out_width 1920 --out_height 1080 -d /data -w 1 -l 540\n", name);
	printf("\t-a | --aiq: iq file path, Default: /etc/iqfiles\n");
	printf("\t-n | --camera_num: camera number, Default:1\n");
	printf("\t--iw: first sensor vi input width, Default:1920\n");
	printf("\t--ih: first sensor vi input height, Default:1080\n");
	printf("\t--ow: first sensor vi output width, Default:1920\n");
	printf("\t--oh: first sensor vi output height, Default:1080\n");
	printf("\t--buf_size: venc buffer size, Default:out_width * out_height\n");
	printf("\t--iw1: second sensor vi input width, Default:1920\n");
	printf("\t--ih1: second sensor vi input height, Default:1080\n");
	printf("\t--ow1: second sensor vi output width, Default:1920\n");
	printf("\t--oh1: second sensor vi output height, Default:1080\n");
	printf("\t--buf_size1: second sensor venc buffer size, Default:out_width_second * out_height_second\n");
	printf("\t--buf_cnt: Vi buf_cnt, Default:2\n");
	printf("\t--pipe_mb_cnt: Vi pipe mediabuffer cnt, Default:1\n");
	printf("\t--frame_cnt: frame number of output, Default:-1\n");
	printf("\t-I | --chnId: vi channel id, Default 0. "
		   "0:rkisp_mainpath,1:rkisp_selfpath,2:rkisp_bypasspath\n");
	printf("\t--devId: vi device id, Default: 0\n");
	printf("\t-v | --vencId: venc channel id, Default: 0\n");
	printf("\t-d | --dir: input/output file directory, Default: /data\n");
	printf("\t-w | --wrap: first sensor enable wrap mode, Default: 1\n");
	printf("\t-l | --wrap_line: config wrap line, Default: 540\n");
	printf("\t-W | --wrap1: second sensor enable wrap mode, Default: 1\n");
	printf("\t-L | --wrap_line1: config second sensor wrap line, Default: 540\n");
	printf("\t-c | --codec: encode type, Default: 8 (h264), Value: 8 (h264), 12 (h265), 9 (mjpeg)\n");
	printf("\t-m | --mode: first sensor test mode, Default: 0(offline), Value: 0(offline), 1(online)\n");
	printf("\t-M | --mode1: second sensor test mode, Default: 0(offline), Value: 0(offline), 1(online)\n");
	printf("\t-b | --bitrate, unit kbps, Default: 10*1024 kbps\n");
	printf("\t-f | --framerate, Default: 30\n");
	printf("\t-F | --framerate1, Default: 30\n");
	printf("\t-r | --Dstframerate, Default: 30\n");
	printf("\t-R | --Dstframerate1, Default: 30\n");
	printf("\t-s | --save, whether to save the output file, Default: save file\n");
	printf("\t--mirror, enable vi mirror\n");
	printf("\t--flip, enable vi flip\n");
	printf("\t--pipe_mem_mode, Default: 1. Value: 0(VI_RAW_MEM_COMPACT), 1(VI_RAW_MEM_WORD_LOW_ALIGN), 2(VI_RAW_MEM_WORD_HIGH_ALIGN)\n");
}
static const struct option long_options[] = {
	{"aiq", optional_argument, NULL, 'a'},
	{"camera_num", required_argument, NULL, 'n'},
	{"iw", required_argument, NULL, 'i' + 'w'},
	{"ih", required_argument, NULL, 'i' + 'h'},
	{"ow", required_argument, NULL, 'o' + 'w'},
	{"oh", required_argument, NULL, 'o' + 'h'},
	{"buf_size", required_argument, NULL, 'b' + 's'},
	{"iw1", required_argument, NULL, 'i' + 'w' + 's'},
	{"ih1", required_argument, NULL, 'i' + 'h' + 's'},
	{"ow1", required_argument, NULL, 'o' + 'w' + 's'},
	{"oh1", required_argument, NULL, 'o' + 'h' + 's'},
	{"buf_size1", required_argument, NULL, 'b' + 's' + 's'},
	{"frame_cnt", required_argument, NULL, 'f' + 'c'},
	{"chnId", required_argument, NULL, 'I'},
	{"devId", required_argument, NULL, 'd' + 'i'},
	{"vencId", required_argument, NULL, 'v'},
	{"dir", required_argument, NULL, 'd'},
	{"wrap", required_argument, NULL, 'w'},
	{"wrap_line", required_argument, NULL, 'l'},
	{"wrap1", required_argument, NULL, 'W'},
	{"wrap_line1", required_argument, NULL, 'L'},
	{"codec", required_argument, NULL, 'c'},
	{"mode", required_argument, NULL, 'm'},
	{"mode1", required_argument, NULL, 'M'},
	{"bitrate", required_argument, NULL, 'b'},
	{"framerate", required_argument, NULL, 'f'},
	{"framerate1", required_argument, NULL, 'F'},
	{"Dstframerate", required_argument, NULL, 'r'},
	{"Dstframerate1", required_argument, NULL, 'R'},
	{"save", required_argument, NULL, 's'},
	{"pipe_mem_mode", required_argument, NULL, 'p' + 'm'},
	{"buf_cnt", required_argument, NULL, 'b' + 'c'},
	{"pipe_mb_cnt", required_argument, NULL, 'p' + 'm' + 'c'},
	{"mirror", no_argument, NULL, 'm' + 'r' + 'r'},
	{"flip", no_argument, NULL, 'f' + 'p'},
	{"opw", required_argument, NULL, 'o' + 'p'+ 'w'},
	{"oph", required_argument, NULL, 'o' + 'p'+ 'h'},
	{"opw1", required_argument, NULL, 'o' + 'p'+ 'w' + 's'},
	{"oph1", required_argument, NULL, 'o' + 'p'+ 'h' + 's'},
	{"help", optional_argument, NULL, '?'},
	{"pw", required_argument, NULL, 'p' + 'w'},
	{"pl", required_argument, NULL, 'p' + 'l'},
	{"pw1", required_argument, NULL, 'p' + 'w' + 's'},
	{"pl1", required_argument, NULL, 'p' + 'l' + 's'},
	{"opf", required_argument, NULL, 'o' + 'p' +'f'},
	{"opf1", required_argument, NULL, 'o' + 'p' + 'f' + 's'},
	{"rf", required_argument, NULL, 'r' + 'f'},
	{NULL, 0, NULL, 0},
};

static XCamReturn SIMPLE_COMM_ISP_SofCb(rk_aiq_metas_t *meta) {
	g_sof_cnt++;
	if (g_sof_cnt <= 2)
		printf("=== %u ===\n", meta->frame_id);
	return XCAM_RETURN_NO_ERROR;
}

static XCamReturn SIMPLE_COMM_ISP_ErrCb(rk_aiq_err_msg_t *msg) {
	if (msg->err_code == XCAM_RETURN_BYPASS)
		g_should_quit = true;
	return XCAM_RETURN_NO_ERROR;
}

static RK_S32 SIMPLE_COMM_ISP_Init(RK_S32 CamId, rk_aiq_working_mode_t WDRMode, RK_BOOL MultiCam, const char *iq_file_dir) {
	if (CamId >= MAX_AIQ_CTX) {
		printf("%s : CamId is over 3\n", __FUNCTION__);
		return -1;
	}

	// char *iq_file_dir = "iqfiles/";
	setlinebuf(stdout);
	if (iq_file_dir == NULL) {
		printf("SIMPLE_COMM_ISP_Init : not start.\n");
		g_aiq_ctx[CamId] = NULL;
		return 0;
	}

	// must set HDR_MODE, before init
	g_WDRMode[CamId] = WDRMode;
	char hdr_str[16];
	snprintf(hdr_str, sizeof(hdr_str), "%d", (int)WDRMode);
	setenv("HDR_MODE", hdr_str, 1);

	rk_aiq_sys_ctx_t *aiq_ctx;
	rk_aiq_static_info_t aiq_static_info;

#ifdef RV1126_RV1109
	rk_aiq_uapi_sysctl_enumStaticMetas(CamId, &aiq_static_info);

	printf("ID: %d, sensor_name is %s, iqfiles is %s\n", CamId, aiq_static_info.sensor_info.sensor_name, iq_file_dir);

	aiq_ctx = rk_aiq_uapi_sysctl_init(aiq_static_info.sensor_info.sensor_name, iq_file_dir,
					SIMPLE_COMM_ISP_ErrCb, SIMPLE_COMM_ISP_SofCb);

	if (MultiCam)
		rk_aiq_uapi_sysctl_setMulCamConc(aiq_ctx, true);
#else
	rk_aiq_uapi2_sysctl_enumStaticMetas(CamId, &aiq_static_info);

	printf("ID: %d, sensor_name is %s, iqfiles is %s\n", CamId, aiq_static_info.sensor_info.sensor_name, iq_file_dir);

	aiq_ctx = rk_aiq_uapi2_sysctl_init(aiq_static_info.sensor_info.sensor_name, iq_file_dir,
					SIMPLE_COMM_ISP_ErrCb, SIMPLE_COMM_ISP_SofCb);

	if (MultiCam)
		rk_aiq_uapi2_sysctl_setMulCamConc(aiq_ctx, true);
#endif

	g_aiq_ctx[CamId] = aiq_ctx;
	return 0;
}

static RK_S32 SIMPLE_COMM_ISP_Run(RK_S32 CamId) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}

#ifdef RV1126_RV1109
	if (rk_aiq_uapi_sysctl_prepare(g_aiq_ctx[CamId], 0, 0, g_WDRMode[CamId])) {
		printf("rkaiq engine prepare failed !\n");
		g_aiq_ctx[CamId] = NULL;
		return -1;
	}
	printf("rk_aiq_uapi_sysctl_init/prepare succeed\n");

	if (rk_aiq_uapi_sysctl_start(g_aiq_ctx[CamId])) {
		printf("rk_aiq_uapi_sysctl_start  failed\n");
		return -1;
	}
	printf("rk_aiq_uapi_sysctl_start succeed\n");
#else
	if (rk_aiq_uapi2_sysctl_prepare(g_aiq_ctx[CamId], 0, 0, g_WDRMode[CamId])) {
		printf("rkaiq engine prepare failed !\n");
		g_aiq_ctx[CamId] = NULL;
		return -1;
	}
	printf("rk_aiq_uapi2_sysctl_init/prepare succeed\n");

	if (rk_aiq_uapi2_sysctl_start(g_aiq_ctx[CamId])) {
		printf("rk_aiq_uapi2_sysctl_start  failed\n");
		return -1;
	}
	printf("rk_aiq_uapi2_sysctl_start succeed\n");
#endif
	return 0;
}

static RK_S32 SIMPLE_COMM_ISP_Stop(RK_S32 CamId) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init g_aiq_ctx[%d] = %p\n", __FUNCTION__, CamId, g_aiq_ctx[CamId]);
		return -1;
	}

#ifdef RV1126_RV1109
	printf("rk_aiq_uapi_sysctl_stop enter\n");
	rk_aiq_uapi_sysctl_stop(g_aiq_ctx[CamId], false);
	printf("rk_aiq_uapi_sysctl_deinit enter\n");
	rk_aiq_uapi_sysctl_deinit(g_aiq_ctx[CamId]);
	printf("rk_aiq_uapi_sysctl_deinit exit\n");
#else
	printf("rk_aiq_uapi2_sysctl_stop enter\n");
	rk_aiq_uapi2_sysctl_stop(g_aiq_ctx[CamId], false);
	printf("rk_aiq_uapi2_sysctl_deinit enter\n");
	rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx[CamId]);
	printf("rk_aiq_uapi2_sysctl_deinit exit\n");
#endif

	g_aiq_ctx[CamId] = NULL;
	return 0;
}

static void init_ctx_cfg() {
	TEST_VI_CFG_S *pViCfg = &stTestCtx.stViCtx[0];
	TEST_VENC_CFG_S *pVencCfg = &stTestCtx.stVencCtx[0];
	TEST_VI_CFG_S *pViCfg1 = &stTestCtx.stViCtx[1];
	TEST_VENC_CFG_S *pVencCfg1 = &stTestCtx.stVencCtx[1];
	TEST_VPSS_CFG_S *pVpssCfg1 = &stTestCtx.stVpssCtx[1];

	memset(&stTestCtx, 0, sizeof(TEST_CTX_S));

	stTestCtx.u32CamNum = 1;
	stTestCtx.firstMode = TEST_OFFLINE_MODE;
	stTestCtx.secondMode = TEST_OFFLINE_MODE;
	pViCfg->u32PipeId = 0;
	pViCfg->u32ChnId = 0;
	pViCfg->u32InWidth = 1920;
	pViCfg->u32InHeight = 1080;
	pViCfg->u32OutWidth = 1920;
	pViCfg->u32OutHeight = 1080;
	pViCfg->u32MaxWidth = pViCfg->u32OutWidth;
	pViCfg->u32MaxHeight = pViCfg->u32OutHeight;
	pViCfg->bEnableWrap = RK_FALSE;
	pViCfg->u32WrapLine = 540;
	pViCfg->u32Framerate = 30;
	pViCfg->u32DstFramerate = 30;
	pViCfg->enMemMode = VI_RAW_MEM_WORD_LOW_ALIGN;
	pViCfg->u32BufCnt = 2;
	pViCfg->u32PipeMbCnt = 1;

	pVencCfg->u32ChnId = 0;
	pVencCfg->enCodecType = RK_VIDEO_ID_AVC;
	pVencCfg->bEnableWrap = RK_FALSE;
	pVencCfg->u32WrapLine = 540;
	pVencCfg->s32FrameCnt = -1;
	pVencCfg->u32Width = 1920;
	pVencCfg->u32Height = 1080;
	pVencCfg->u32Bitrate = 10 * 1024;
	pVencCfg->u32Framerate = 30;

	memcpy(pViCfg1, pViCfg, sizeof(TEST_VI_CFG_S));
	memcpy(pVencCfg1, pVencCfg, sizeof(TEST_VENC_CFG_S));

	pViCfg1->u32PipeId = 1;
	pVencCfg1->u32ChnId = 1;
	pVpssCfg1->s32Grp = 1;
	pVpssCfg1->s32ChnId = 0;
}

static void open_file(RK_U32 u32CamNum, const RK_CHAR *pfileDir, bool bSaveFile,
					  RK_CHAR *pinFile1, RK_CHAR *pinFile2,
					  RK_CHAR *pinFile3, RK_CHAR *pinFile4,
					  RK_CHAR *pOutfileDir, RK_CHAR *pOutfileDir1) {
	char inputPath[256], outputPath[256];
	const char *postfix = "h264";
	TEST_VI_CFG_S *pViCfg = &stTestCtx.stViCtx[0];
	TEST_VPSS_CFG_S *pVpssCfg = &stTestCtx.stVpssCtx[0];
	TEST_VENC_CFG_S *pVencCfg = &stTestCtx.stVencCtx[0];
	TEST_VI_CFG_S *pViCfg1 = &stTestCtx.stViCtx[1];
	TEST_VPSS_CFG_S *pVpssCfg1 = &stTestCtx.stVpssCtx[1];
	TEST_VENC_CFG_S *pVencCfg1 = &stTestCtx.stVencCtx[1];

	if (stTestCtx.firstMode == TEST_OFFLINE_MODE) {
		memset(inputPath, 0, 256);
		sprintf(inputPath, "%s/%s", pfileDir, pinFile1);
		printf("#Input path: %s\n", inputPath);

		pViCfg->fp = fopen(inputPath, "rb");
		if (!pViCfg->fp)
			printf("open input file(%s) failed, errno[%d, %s]\n", inputPath, errno, strerror(errno));

		memset(inputPath, 0, 256);
		sprintf(inputPath, "%s/%s", pfileDir, pinFile2);
		printf("#Input second path: %s\n", inputPath);

		pViCfg->fp1 = fopen(inputPath, "rb");
		if (!pViCfg->fp1)
			printf("open input second file(%s) failed, errno[%d, %s]\n", inputPath, errno, strerror(errno));
	}

	if (pVencCfg->enCodecType == RK_VIDEO_ID_AVC)
		postfix = "h264";
	else if (pVencCfg->enCodecType == RK_VIDEO_ID_HEVC)
		postfix = "h265";
	else if (pVencCfg->enCodecType == RK_VIDEO_ID_JPEG)
		postfix = "jpg";
	else if (pVencCfg->enCodecType == RK_VIDEO_ID_MJPEG)
		postfix = "mjpg";
	else {
		printf("nonsupport codec type: %d\n", pVencCfg->enCodecType);
	}

	if (bSaveFile) {
		memset(outputPath, 0, 256);
		sprintf(outputPath, "%s/vpss_chn%d_%dx%d_%d.%s", pOutfileDir, pVencCfg->u32ChnId,
			pVencCfg->u32Width, pVencCfg->u32Height, pVencCfg->u32WrapLine, postfix);
		printf("#Output path: %s\n", outputPath);

		pVencCfg->fp = fopen(outputPath, "w");
		if (!pVencCfg->fp)
			printf("open output file(%s) failed, errno[%d, %s]\n", outputPath, errno, strerror(errno));
	}

	if (u32CamNum == 2) {
		if (stTestCtx.secondMode == TEST_OFFLINE_MODE) {
			memset(inputPath, 0, 256);
			if (strcmp(pinFile3, " ") == 0)
				sprintf(inputPath, "%s/%s", pfileDir, pinFile1);
			else
				sprintf(inputPath, "%s/%s", pfileDir, pinFile3);
			printf("#Input path: %s\n", inputPath);

			pViCfg1->fp = fopen(inputPath, "rb");
			if (!pViCfg1->fp)
				printf("open input file(%s) failed, errno[%d, %s]\n", inputPath, errno, strerror(errno));

			memset(inputPath, 0, 256);
			if (strcmp(pinFile4, " ") == 0)
				sprintf(inputPath, "%s/%s", pfileDir, pinFile2);
			else
				sprintf(inputPath, "%s/%s", pfileDir, pinFile4);
			printf("#Input second path: %s\n", inputPath);

			pViCfg1->fp1 = fopen(inputPath, "rb");
			if (!pViCfg1->fp1)
				printf("open input second file(%s) failed, errno[%d, %s]\n", inputPath, errno, strerror(errno));
		}

		if (bSaveFile) {
			memset(outputPath, 0, 256);
			sprintf(outputPath, "%s/vpss_chn%d_%dx%d_%d.%s", pOutfileDir1, pVencCfg1->u32ChnId,
				pVencCfg1->u32Width, pVencCfg1->u32Height, pVencCfg1->u32WrapLine, postfix);
			printf("#Second output path: %s\n", outputPath);

			pVencCfg1->fp = fopen(outputPath, "w");
			if (!pVencCfg1->fp)
				printf("open second output file(%s) failed, errno[%d, %s]\n", outputPath, errno, strerror(errno));
		}
	}
}

static void close_file(RK_U32 u32CamNum) {
	TEST_VI_CFG_S *pViCfg = &stTestCtx.stViCtx[0];
	TEST_VENC_CFG_S *pVencCfg = &stTestCtx.stVencCtx[0];
	TEST_VI_CFG_S *pViCfg1 = &stTestCtx.stViCtx[1];
	TEST_VENC_CFG_S *pVencCfg1 = &stTestCtx.stVencCtx[1];

	if (pViCfg->fp) {
		printf("close vi[%d, %d] input file\n", pViCfg->u32PipeId, pViCfg->u32ChnId);
		fclose(pViCfg->fp);
	}

	if (pVencCfg->fp) {
		printf("close venc[%d] output file\n", pVencCfg->u32ChnId);
		fclose(pVencCfg->fp);
	}

	if (u32CamNum == 2) {
		if (pViCfg1->fp) {
		printf("close vi[%d, %d] input file\n", pViCfg1->u32PipeId, pViCfg1->u32ChnId);
			fclose(pViCfg1->fp);
		}

		if (pVencCfg1->fp) {
			printf("close venc[%d] output file\n", pVencCfg1->u32ChnId);
			fclose(pVencCfg1->fp);
		}
	}
}

static int init_all_vi_wrap(RK_U32 u32CamNum) {
	int ret;
	TEST_VI_CFG_S *pViCfg = &stTestCtx.stViCtx[0];
	TEST_VI_CFG_S *pViCfg1 = &stTestCtx.stViCtx[1];

	if (stTestCtx.firstMode == TEST_ONLINE_MODE) {
		ret = vi_dev_init(pViCfg->u32PipeId);
		if (ret) {
			printf("vi_dev_init[%d] failed[%x]", pViCfg->u32PipeId, ret);
			return -1;
		}
	} else {
		ret = vi_pipe_init(pViCfg);
		if (ret) {
			printf("vi_pipe_init[%d] failed[%x]", pViCfg->u32PipeId, ret);
			return -1;
		}
	}
	printf("%d: pipe or dev init ok\n", pViCfg->u32PipeId);

	if (u32CamNum > 1) {
		if (stTestCtx.secondMode == TEST_ONLINE_MODE) {
			ret = vi_dev_init(pViCfg1->u32PipeId);
			if (ret) {
				printf("vi_dev_init[%d] failed[%x]", pViCfg1->u32PipeId, ret);
				return -1;
			}
		} else {
			ret = vi_pipe_init(pViCfg1);
			if (ret) {
				printf("vi_pipe_init[%d] failed[%x]", pViCfg1->u32PipeId, ret);
				return -1;
			}
		}
		printf("%d: pipe or dev init ok\n", pViCfg1->u32PipeId);
	}

	if (stTestCtx.firstMode == TEST_OFFLINE_MODE) {
		ret = RK_MPI_VI_StartPipe(pViCfg->u32PipeId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_StartPipe[%d] failed: %x\n", pViCfg->u32PipeId, ret);
			return -1;
		}
		printf("%d: start pipe ok\n", pViCfg->u32PipeId);
	}

	if (u32CamNum > 1 && stTestCtx.secondMode == TEST_OFFLINE_MODE) {
		ret = RK_MPI_VI_StartPipe(pViCfg1->u32PipeId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_StartPipe[%d] failed: %x\n", pViCfg1->u32PipeId, ret);
			return -1;
		}
		printf("%d: start pipe ok\n", pViCfg1->u32PipeId);
	}

	ret = vi_chn_init(pViCfg);
	if (ret) {
		printf("vi_chn_init[%d, %d] failed[%x]", pViCfg->u32PipeId, pViCfg->u32ChnId, ret);
		return -1;
	}
	printf("vi chn[%d, %d] init ok\n", pViCfg->u32PipeId, pViCfg->u32ChnId);

	if (u32CamNum > 1) {
		ret = vi_chn_init(pViCfg1);
		if (ret) {
			printf("vi_chn_init[%d, %d] failed[%x]", pViCfg1->u32PipeId, pViCfg1->u32ChnId, ret);
			return -1;
		}
		printf("vi chn[%d, %d] init ok\n", pViCfg1->u32PipeId, pViCfg1->u32ChnId);
	}

	return 0;
}

static int deinit_all_vi_wrap(RK_U32 u32CamNum) {
	int ret = 0;
	TEST_VI_CFG_S *pViCfg = &stTestCtx.stViCtx[0];
	TEST_VI_CFG_S *pViCfg1 = &stTestCtx.stViCtx[1];

	ret = RK_MPI_VI_DisableChn(pViCfg->u32PipeId, pViCfg->u32ChnId);
	printf("RK_MPI_VI_DisableChn[%d, %d]: %x\n", pViCfg->u32PipeId, pViCfg->u32ChnId, ret);

	if (u32CamNum > 1) {
		ret = RK_MPI_VI_DisableChn(pViCfg1->u32PipeId, pViCfg1->u32ChnId);
		printf("RK_MPI_VI_DisableChn[%d, %d]: %x\n", pViCfg1->u32PipeId, pViCfg1->u32ChnId, ret);
	}

	if (stTestCtx.firstMode == TEST_ONLINE_MODE) {
		ret = RK_MPI_VI_DisableDev(pViCfg->u32ChnId);
		printf("RK_MPI_VI_DisableDev[%d]: %x\n", pViCfg->u32ChnId, ret);
	} else {
		ret = RK_MPI_VI_StopPipe(pViCfg->u32PipeId);
		printf("RK_MPI_VI_StopPipe[%d]: %x\n", pViCfg->u32ChnId, ret);
		ret = RK_MPI_VI_DestroyPipe(pViCfg->u32PipeId);
		printf("RK_MPI_VI_DestroyPipe[%d]: %x\n", pViCfg->u32ChnId, ret);
	}

	if (u32CamNum > 1) {
		if (stTestCtx.secondMode == TEST_ONLINE_MODE) {
			ret = RK_MPI_VI_DisableDev(pViCfg1->u32ChnId);
			printf("RK_MPI_VI_DisableDev[%d]: %x\n", pViCfg1->u32ChnId, ret);
		} else {
			ret = RK_MPI_VI_StopPipe(pViCfg1->u32PipeId);
			printf("RK_MPI_VI_StopPipe[%d]: %x\n", pViCfg1->u32ChnId, ret);
			ret = RK_MPI_VI_DestroyPipe(pViCfg1->u32PipeId);
			printf("RK_MPI_VI_DestroyPipe[%d]: %x\n", pViCfg1->u32ChnId, ret);
		}
	}

	return ret;
}

static int init_vi_venc_wrap(TEST_VI_CFG_S *pViCfg, TEST_VENC_CFG_S *pVencCfg, TEST_LINE_MODE mode) {
	int ret = 0;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;

	ret = venc_chn_init(pVencCfg);
	if (ret != RK_SUCCESS) {
		printf("create %d ch venc failed\n", pVencCfg->u32ChnId);
		return -1;
	}
	printf("venc chn[%d] init ok\n", pVencCfg->u32ChnId);

	ret = pthread_create(&pVencCfg->tid, NULL, get_venc_stream, pVencCfg);
	if (ret) {
		printf("Create get venc mb(%d) thread failed %d\n", pVencCfg->u32ChnId, ret);
		goto _FAILED;
	}

	// bind vi to venc
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = pViCfg->u32PipeId;
	stSrcChn.s32ChnId = pViCfg->u32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = pVencCfg->u32ChnId;
	ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (ret != RK_SUCCESS) {
		printf("vi[%d] bind venc[%d] ch venc failed\n", stSrcChn.s32ChnId, stDestChn.s32ChnId);
		goto _FAILED_1;
	}
	printf("vi[%d, %d] bind venc[%d] ok\n", pViCfg->u32PipeId, pViCfg->u32ChnId, pVencCfg->u32ChnId);

	if (mode == TEST_OFFLINE_MODE) {
		ret = pthread_create(&pViCfg->tid, NULL, send_raw_frame, pViCfg);
		if (ret) {
			printf("Create get venc mb(%d) thread failed %d\n", pViCfg->u32ChnId, ret);
			goto _FAILED;
		}
	}

	return 0;

_FAILED:
	if (pVencCfg->tid) {
		ret = pthread_join(pVencCfg->tid, NULL);
		if (ret)
			printf("Exit get venc[%d] mb thread failed!\n", pVencCfg->u32ChnId);
		else
			printf("Exit get venc[%d] mb thread ok\n", pVencCfg->u32ChnId);
		pVencCfg->tid = 0;
	}

	if (pViCfg->tid) {
		ret = pthread_join(pViCfg->tid, NULL);
		if (ret)
			printf("Exit send vi[%d] frame thread failed!\n", pViCfg->u32ChnId);
		else
			printf("Exit send vi[%d] frame thread ok\n", pViCfg->u32ChnId);
		pViCfg->tid = 0;
	}

	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	printf("RK_MPI_SYS_UnBind[%d, %d]: %x\n", stSrcChn.s32ChnId, stDestChn.s32ChnId, ret);

_FAILED_1:
	ret = RK_MPI_VENC_DestroyChn(pVencCfg->u32ChnId);
	printf("RK_MPI_VENC_DestroyChn[%d]: %x\n", pVencCfg->u32ChnId, ret);

	return -1;
}

static int deinit_vi_venc_wrap(TEST_VI_CFG_S *pViCfg, TEST_VENC_CFG_S *pVencCfg, TEST_LINE_MODE mode) {
	int ret = 0;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;

	if (pVencCfg->tid) {
		ret = pthread_join(pVencCfg->tid, NULL);
		if (ret)
			printf("Exit get venc[%d] mb thread failed!\n", pVencCfg->u32ChnId);
		else
			printf("Exit get venc[%d] mb thread ok\n", pVencCfg->u32ChnId);
		pVencCfg->tid = 0;
	}

	if (pViCfg->tid) {
		ret = pthread_join(pViCfg->tid, NULL);
		if (ret)
			printf("Exit send vi[%d] frame thread failed!\n", pViCfg->u32ChnId);
		else
			printf("Exit send vi[%d] frame thread ok\n", pViCfg->u32ChnId);
		pViCfg->tid = 0;
	}

	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = pViCfg->u32PipeId;
	stSrcChn.s32ChnId = pViCfg->u32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = pVencCfg->u32ChnId;
	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	printf("RK_MPI_SYS_UnBind[%d, %d]: %x\n", stSrcChn.s32ChnId, stDestChn.s32ChnId, ret);

	system("cat /dev/mpi/valloc");
	system("cat /dev/mpi/vsys");
	ret = RK_MPI_VENC_DestroyChn(pVencCfg->u32ChnId);
	printf("RK_MPI_VENC_DestroyChn[%d]: %x\n", pVencCfg->u32ChnId, ret);

	return ret;
}

static int init_venc_wrap(TEST_VI_CFG_S *pViCfg, TEST_VENC_CFG_S *pVencCfg, TEST_VPSS_CFG_S *pVpssCfg, TEST_LINE_MODE mode) {
	int ret = 0;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;

	// init vpss
	ret = vpss_chn_init(pVpssCfg);
	if (ret != RK_SUCCESS) {
		RK_LOGE("creat 0 grp vpss failed!");
		return -1;
	}

	ret = venc_chn_init(pVencCfg);
	if (ret != RK_SUCCESS) {
		printf("create %d ch venc failed\n", pVencCfg->u32ChnId);
		return -1;
	}
	printf("venc chn[%d] init ok\n", pVencCfg->u32ChnId);

	system("cat /dev/mpi/valloc");
	ret = pthread_create(&pVencCfg->tid, NULL, get_venc_stream, pVencCfg);
	if (ret) {
		printf("Create get venc mb(%d) thread failed %d\n", pVencCfg->u32ChnId, ret);
		goto _FAILED;
	}

	// bind vi to venc
	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = pVpssCfg->s32Grp;
	stSrcChn.s32ChnId = pVpssCfg->s32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = pVencCfg->u32ChnId;
	ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (ret != RK_SUCCESS) {
		printf("vpss[%d] bind venc[%d] ch venc failed\n", stSrcChn.s32ChnId, stDestChn.s32ChnId);
		goto _FAILED_1;
	}
	printf("vpss[%d %d] bind venc[%d] ok\n", pVpssCfg->s32Grp, pVpssCfg->s32ChnId, pVencCfg->u32ChnId);

	// bind vi to venc
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = pViCfg->u32PipeId;
	stSrcChn.s32ChnId = pViCfg->u32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = pVpssCfg->s32Grp;
	stDestChn.s32ChnId = 0;
	ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (ret != RK_SUCCESS) {
		printf("vi[%d] bind vpss[%d %d] ch vpss failed\n", stSrcChn.s32ChnId, stDestChn.s32DevId, stDestChn.s32ChnId);
		goto _FAILED_1;
	}
	printf("vi[%d, %d] bind vpss[%d %d] ok\n", pViCfg->u32PipeId, pViCfg->u32ChnId, stDestChn.s32DevId, stDestChn.s32ChnId);

	if (mode == TEST_OFFLINE_MODE) {
		ret = pthread_create(&pViCfg->tid, NULL, send_raw_frame, pViCfg);
		if (ret) {
			printf("Create get venc mb(%d) thread failed %d\n", pViCfg->u32ChnId, ret);
			goto _FAILED;
		}
	}

	return 0;

_FAILED:
	if (pVencCfg->tid) {
		ret = pthread_join(pVencCfg->tid, NULL);
		if (ret)
			printf("Exit get venc[%d] mb thread failed!\n", pVencCfg->u32ChnId);
		else
			printf("Exit get venc[%d] mb thread ok\n", pVencCfg->u32ChnId);
		pVencCfg->tid = 0;
	}

	if (pViCfg->tid) {
		ret = pthread_join(pViCfg->tid, NULL);
		if (ret)
			printf("Exit send vi[%d] frame thread failed!\n", pViCfg->u32ChnId);
		else
			printf("Exit send vi[%d] frame thread ok\n", pViCfg->u32ChnId);
		pViCfg->tid = 0;
	}

	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	printf("RK_MPI_SYS_UnBind[%d, %d]: %x\n", stSrcChn.s32ChnId, stDestChn.s32ChnId, ret);

_FAILED_1:
	ret = RK_MPI_VENC_DestroyChn(pVencCfg->u32ChnId);
	printf("RK_MPI_VENC_DestroyChn[%d]: %x\n", pVencCfg->u32ChnId, ret);
	vpss_chn_deinit(pVpssCfg->s32Grp, pVpssCfg->u32VpssChnCnt);
	return -1;
}

static int deinit_venc_wrap(TEST_VI_CFG_S *pViCfg, TEST_VENC_CFG_S *pVencCfg, TEST_VPSS_CFG_S *pVpssCfg, TEST_LINE_MODE mode) {
	int ret = 0;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;
	if (pVencCfg->tid) {
		ret = pthread_join(pVencCfg->tid, NULL);
		if (ret)
			printf("Exit get venc[%d] mb thread failed!\n", pVencCfg->u32ChnId);
		else
			printf("Exit get venc[%d] mb thread ok\n", pVencCfg->u32ChnId);
		pVencCfg->tid = 0;
	}

	if (pViCfg->tid) {
		ret = pthread_join(pViCfg->tid, NULL);
		if (ret)
			printf("Exit send vi[%d] frame thread failed!\n", pViCfg->u32ChnId);
		else
			printf("Exit send vi[%d] frame thread ok\n", pViCfg->u32ChnId);
		pViCfg->tid = 0;
	}

	stSrcChn.enModId = RK_ID_VPSS;
	stSrcChn.s32DevId = pVpssCfg->s32Grp;
	stSrcChn.s32ChnId = pVpssCfg->s32ChnId;
	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = pVencCfg->u32ChnId;
	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	printf("RK_MPI_SYS_UnBind VPSS VENC[%d, %d]: %x\n", stSrcChn.s32ChnId, stDestChn.s32ChnId, ret);

	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = pViCfg->u32PipeId;
	stSrcChn.s32ChnId = pViCfg->u32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = pVpssCfg->s32Grp;
	stDestChn.s32ChnId = 0;
	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	printf("RK_MPI_SYS_UnBind VI VPSS[%d, %d]: %x\n", stSrcChn.s32ChnId, stDestChn.s32ChnId, ret);

	system("cat /dev/mpi/valloc");
	system("cat /dev/mpi/vsys");
	ret = RK_MPI_VENC_DestroyChn(pVencCfg->u32ChnId);
	printf("RK_MPI_VENC_DestroyChn[%d]: %x\n", pVencCfg->u32ChnId, ret);
	vpss_chn_deinit(pVpssCfg->s32Grp, pVpssCfg->u32VpssChnCnt);
	return ret;
}

static int init_vpss(TEST_VI_CFG_S *pViCfg, TEST_VPSS_CFG_S *pVpssCfg, TEST_LINE_MODE mode) {
	int ret = 0;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;

	// init vpss
	ret = vpss_chn_init(pVpssCfg);
	if (ret != RK_SUCCESS) {
		RK_LOGE("creat 0 grp vpss failed!");
		return -1;
	}

	ret = pthread_create(&pVpssCfg->tid, NULL, get_vpss_frame, pVpssCfg);
	if (ret) {
		printf("Create get venc mb(%d) thread failed %d\n", pVpssCfg->s32ChnId, ret);
		goto _FAILED;
	}

	// bind vi to venc
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = pViCfg->u32PipeId;
	stSrcChn.s32ChnId = pViCfg->u32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = pVpssCfg->s32Grp;
	stDestChn.s32ChnId = 0;
	ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (ret != RK_SUCCESS) {
		printf("vi[%d] bind vpss[%d] ch vpss failed\n", stSrcChn.s32ChnId, stDestChn.s32ChnId);
		goto _FAILED_1;
	}
	printf("vi[%d, %d] bind vpss[%d] ok\n", pViCfg->u32PipeId, pViCfg->u32ChnId, pVpssCfg->s32ChnId);

	if (mode == TEST_OFFLINE_MODE) {
		ret = pthread_create(&pViCfg->tid, NULL, send_raw_frame, pViCfg);
		if (ret) {
			printf("Create send raw frame(%d) thread failed %d\n", pViCfg->u32ChnId, ret);
			goto _FAILED_1;
		}
	}

	return 0;
_FAILED_1:
	if (pVpssCfg->tid) {
		ret = pthread_join(pVpssCfg->tid, NULL);
		if (ret)
			printf("Exit get vpss[%d] thread failed!\n", pVpssCfg->s32ChnId);
		else
			printf("Exit get vpss[%d] thread ok\n", pVpssCfg->s32ChnId);
		pVpssCfg->tid = 0;
	}
_FAILED:
	vpss_chn_deinit(pVpssCfg->s32Grp, pVpssCfg->u32VpssChnCnt);
	return -1;
}

static int deinit_vpss(TEST_VI_CFG_S *pViCfg, TEST_VPSS_CFG_S *pVpssCfg) {
	int ret = 0;
	MPP_CHN_S stSrcChn;
	MPP_CHN_S stDestChn;

	if (pVpssCfg->tid) {
		ret = pthread_join(pVpssCfg->tid, NULL);
		if (ret)
			printf("Exit get venc[%d] mb thread failed!\n", pVpssCfg->s32ChnId);
		else
			printf("Exit get venc[%d] mb thread ok\n", pVpssCfg->s32ChnId);
		pVpssCfg->tid = 0;
	}

	if (pViCfg->tid) {
		ret = pthread_join(pViCfg->tid, NULL);
		if (ret)
			printf("Exit send vi[%d] frame thread failed!\n", pViCfg->u32ChnId);
		else
			printf("Exit send vi[%d] frame thread ok\n", pViCfg->u32ChnId);
		pViCfg->tid = 0;
	}

	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = pViCfg->u32PipeId;
	stSrcChn.s32ChnId = pViCfg->u32ChnId;
	stDestChn.enModId = RK_ID_VPSS;
	stDestChn.s32DevId = pVpssCfg->s32Grp;
	stDestChn.s32ChnId = 0;
	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	printf("RK_MPI_SYS_UnBind[%d, %d]: %x\n", stSrcChn.s32ChnId, stDestChn.s32ChnId, ret);

	vpss_chn_deinit(pVpssCfg->s32Grp, pVpssCfg->u32VpssChnCnt);
	return ret;
}

int main(int argc, char *argv[]) {
	int c = 0, ret = 0;
	pthread_t tid = 0;
	const RK_CHAR *pfileDir = "/mnt/sdcard";
	RK_CHAR *pinFile1 = "1920x1080_raw_1.raw";
	RK_CHAR *pinFile2 = "1920x1080_raw_2.raw";
	RK_CHAR *pinFile3 = " ";
	RK_CHAR *pinFile4 = " ";
	RK_CHAR *outFileDir = "/tmp";
	RK_CHAR *outFileDir1 = "/tmp";
	RK_CHAR *pIqfilesPath = NULL;
	TEST_VI_CFG_S *pViCfg = &stTestCtx.stViCtx[0];
	TEST_VENC_CFG_S *pVencCfg = &stTestCtx.stVencCtx[0];
	TEST_VPSS_CFG_S *pVpssCfg = &stTestCtx.stVpssCtx[0];
	TEST_VI_CFG_S *pViCfg1 = &stTestCtx.stViCtx[1];
	TEST_VENC_CFG_S *pVencCfg1 = &stTestCtx.stVencCtx[1];
	TEST_VPSS_CFG_S *pVpssCfg1 = &stTestCtx.stVpssCtx[1];

	bool bSaveFile = true;
	RK_S32 s32Ret = 0;

	init_ctx_cfg();

	while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
		switch (c) {
		case 'a':
			pIqfilesPath = optarg;
			break;
		case 'n':
			stTestCtx.u32CamNum = atoi(optarg);
			break;
		case 's':
			bSaveFile = atoi(optarg);
			break;
		case 'v':
			pVencCfg->u32ChnId = atoi(optarg);
			break;
		case 'i' + 'w':
			pViCfg->u32InWidth = atoi(optarg);
			break;
		case 'i' + 'h':
			pViCfg->u32InHeight = atoi(optarg);
			break;
		case 'o' + 'w':
			pViCfg->u32OutWidth = atoi(optarg);
			break;
		case 'o' + 'h':
			pViCfg->u32OutHeight = atoi(optarg);
			break;
		case 'b' + 's':
			pVencCfg->u32BufSize = atoi(optarg);
			break;
		case 'i' + 'w' + 's':
			pViCfg1->u32InWidth = atoi(optarg);
			break;
		case 'i' + 'h' + 's':
			pViCfg1->u32InHeight = atoi(optarg);
			break;
		case 'o' + 'w' + 's':
			pViCfg1->u32OutWidth = atoi(optarg);
			pVencCfg1->u32Width = pViCfg1->u32OutWidth;
			break;
		case 'o' + 'h' + 's':
			pViCfg1->u32OutHeight = atoi(optarg);
			pVencCfg1->u32Height = pViCfg1->u32OutHeight;
			break;
		case 'b' + 's' + 's':
			pVencCfg1->u32BufSize = atoi(optarg);
			break;
		case 'p' + 'm':
			pViCfg->enMemMode = atoi(optarg);
			pViCfg1->enMemMode = pViCfg->enMemMode;
			break;
		case 'p' + 'm' + 'c':
			pViCfg->u32PipeMbCnt = atoi(optarg);
			pViCfg1->u32PipeMbCnt = pViCfg->u32PipeMbCnt;
			break;
		case 'b' + 'c':
			pViCfg->u32BufCnt = atoi(optarg);
			pViCfg1->u32BufCnt = pViCfg->u32BufCnt;
			break;
		case 'm' + 'r' + 'r':
			pViCfg->bMirror = RK_TRUE;
			pViCfg1->bMirror = pViCfg->bMirror;
			break;
		case 'f' + 'p':
			pViCfg->bFlip = RK_TRUE;
			pViCfg1->bFlip = pViCfg->bFlip;
			break;
		case 'I':
			pViCfg->u32ChnId = atoi(optarg);
			pViCfg1->u32ChnId = pViCfg->u32ChnId;
			break;
		case 'd' + 'i':
			pViCfg->u32PipeId = atoi(optarg);
			break;
		case 'c': // codec
			pVencCfg->enCodecType = atoi(optarg);
			pVencCfg1->enCodecType = pVencCfg->enCodecType;
			break;
		case 'b':
			pVencCfg->u32Bitrate = atoi(optarg);
			pVencCfg1->u32Bitrate = pVencCfg->u32Bitrate;
			break;
		case 'N':
			g_s32FrameCnt = atoi(optarg);
			pVencCfg->s32FrameCnt = atoi(optarg);
			pVencCfg1->s32FrameCnt = pVencCfg->s32FrameCnt;
			break;
		case 'd':
			pfileDir = optarg;
			break;
		case 'w':
			pViCfg->bEnableWrap = (RK_BOOL)atoi(optarg);
			pVencCfg->bEnableWrap = pViCfg->bEnableWrap;
			break;
		case 'W':
			pViCfg1->bEnableWrap = (RK_BOOL)atoi(optarg);
			pVencCfg1->bEnableWrap = pViCfg1->bEnableWrap;
			break;
		case 'l':
			pViCfg->u32WrapLine = atoi(optarg);
			pVencCfg->u32WrapLine = pViCfg->u32WrapLine;
			break;
		case 'L':
			pViCfg1->u32WrapLine = atoi(optarg);
			pVencCfg1->u32WrapLine = pViCfg1->u32WrapLine;
			break;
		case 'f':
			pViCfg->u32Framerate = atoi(optarg);
			break;
		case 'F':
			pViCfg1->u32Framerate =  atoi(optarg);
			break;
		case 'r':
			pViCfg->u32DstFramerate = atoi(optarg);
			pVencCfg->u32Framerate = pViCfg->u32DstFramerate;
			break;
		case 'R':
			pViCfg1->u32DstFramerate =  atoi(optarg);
			pVencCfg1->u32Framerate = pViCfg1->u32DstFramerate;
			break;
		case 'm':
			stTestCtx.firstMode = (TEST_LINE_MODE)atoi(optarg);
			break;
		case 'M':
			stTestCtx.secondMode = (TEST_LINE_MODE)atoi(optarg);
			break;
		case 't':
			stTestCtx.viMode = (TEST_VI_MODE)atoi(optarg);
			break;
		case 'e':
			stTestCtx.stVpssCtx->bGrpCropEn = atoi(optarg);
			break;
		case 'E':
			stTestCtx.stVpssCtx->bChnCropEn = atoi(optarg);
			break;
		case 'p':
			stTestCtx.stVpssCtx->s32GrpCropRatio = atoi(optarg);
			break;
		case 'P':
			stTestCtx.stVpssCtx->s32ChnCropRatio = atoi(optarg);
			break;
		case 'y':
			stTestCtx.stVpssCtx->s32GrpMirror = atoi(optarg);
			break;
		case 'Y':
			stTestCtx.stVpssCtx->s32Mirror = atoi(optarg);
			break;
		case 'q':
			stTestCtx.stVpssCtx->s32GrpFlip = atoi(optarg);
			break;
		case 'Q':
			stTestCtx.stVpssCtx->s32Flip = atoi(optarg);
			break;
		case 'k':
			pinFile1 = optarg;
			break;
		case 'K':
			pinFile2 = optarg;
			break;
		case 'o' + 'p' + 'w':
			pVpssCfg->u32Width = atoi(optarg);
			break;
		case 'o' + 'p' + 'h':
			pVpssCfg->u32Height = atoi(optarg);
			break;
		case 'o' + 'p' + 'w' + 's':
			pVpssCfg1->u32Width = atoi(optarg);
			break;
		case 'o' + 'p' + 'h' + 's':
			pVpssCfg1->u32Height = atoi(optarg);
			break;
		case 'p' + 'w':
			pVpssCfg->bEnableWrap = (RK_BOOL)atoi(optarg);
			pVencCfg->bEnableWrap = pVpssCfg->bEnableWrap;
			break;
		case 'p' + 'l':
			pVpssCfg->u32WrapLine = atoi(optarg);
			pVencCfg->u32WrapLine = pVpssCfg->u32WrapLine;
			break;
		case 'p' + 'w' + 's':
			pVpssCfg1->bEnableWrap = (RK_BOOL)atoi(optarg);
			pVencCfg1->bEnableWrap = pVpssCfg1->bEnableWrap;
			break;
		case 'p' + 'l' + 's':
			pVpssCfg1->u32WrapLine = atoi(optarg);
			pVencCfg1->u32WrapLine = pVpssCfg1->u32WrapLine;
			break;
		case 'x':
			pinFile3 = optarg;
			break;
		case 'X':
			pinFile4 = optarg;
			break;
		case 'o' + 'p' + 'f':
			outFileDir = optarg;
			break;
		case 'o' + 'p' + 'f' + 's':
			outFileDir1 = optarg;
			break;
		case 'r' + 'f':
			pVpssCfg->s32DstCompressMode = atoi(optarg);
			break;
		case 'r' + 'f' + 's':
			pVpssCfg1->s32DstCompressMode = atoi(optarg);
			break;
		case '?':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	if (stTestCtx.u32CamNum == 2) {
		pViCfg->u32MaxWidth = pViCfg->u32OutWidth > pViCfg1->u32OutWidth ? pViCfg->u32OutWidth : pViCfg1->u32OutWidth;
		pViCfg->u32MaxHeight = pViCfg->u32OutHeight > pViCfg1->u32OutHeight ? pViCfg->u32OutHeight : pViCfg1->u32OutHeight;
		pViCfg1->u32MaxWidth = pViCfg->u32MaxWidth;
		pViCfg1->u32MaxHeight = pViCfg->u32MaxHeight;
	} else {
		pViCfg->u32MaxWidth = pViCfg->u32OutWidth;
		pViCfg->u32MaxHeight = pViCfg->u32OutHeight;
	}

	u32CamNum = stTestCtx.u32CamNum;

	if (!pVpssCfg->u32Width) {
		pVpssCfg->u32Width = pViCfg->u32OutWidth;
	}

	if (!pVpssCfg->u32Height) {
		pVpssCfg->u32Height = pViCfg->u32OutHeight;
	}

	if (!pVpssCfg1->u32Width) {
		pVpssCfg1->u32Width = pViCfg1->u32OutWidth;
	}

	if (!pVpssCfg1->u32Height) {
		pVpssCfg1->u32Height = pViCfg1->u32OutHeight;
	}

	pVencCfg->u32Width = pVpssCfg->u32Width;
	pVencCfg->u32Height = pVpssCfg->u32Height;
	pVencCfg1->u32Width = pVpssCfg1->u32Width;
	pVencCfg1->u32Height = pVpssCfg1->u32Height;
	if (pVencCfg->u32BufSize == 0)
		pVencCfg->u32BufSize = pVencCfg->u32Width * pVencCfg->u32Height;

	if (pVencCfg1->u32BufSize == 0)
		pVencCfg1->u32BufSize = pVencCfg1->u32Width * pVencCfg1->u32Height;

	printf("#u32CamNum: %d, pViCfg->u32PipeId: %d, pViCfg->u32ChnId: %d, pViCfg1->u32PipeId: %d\n", stTestCtx.u32CamNum, pViCfg->u32PipeId, pViCfg->u32ChnId, pViCfg1->u32PipeId);
	printf("#pVencCfg->u32ChnId: %d, pVencCfg1->u32ChnId: %d\n", pVencCfg->u32ChnId, pVencCfg1->u32ChnId);
	printf("#enMemMode: %d, u32BufCnt: %d, u32PipeMbCnt: %d\n", pViCfg->enMemMode, pViCfg->u32BufCnt, pViCfg->u32PipeMbCnt);
	printf("#u32MaxWidth: %d, u32MaxHeight: %d\n", pViCfg->u32MaxWidth, pViCfg->u32MaxHeight);
	printf("#Resolution: In: %dx%d, Out: %dx%d\n", pViCfg->u32InWidth, pViCfg->u32InHeight, pViCfg->u32OutWidth, pViCfg->u32OutHeight);
	printf("#vi bWrapEn: %d, u32WrapLine: %d\n", pViCfg->bEnableWrap, pViCfg->u32WrapLine);
	printf("#u32BufSize: %d, second u32BufSize: %d\n", pVencCfg->u32BufSize, pVencCfg1->u32BufSize);
	printf("#save file dir: %s, bSaveFile: %d\n", pfileDir, bSaveFile);
	printf("#firstMode: %d, secondMode: %d\n", stTestCtx.firstMode, stTestCtx.secondMode);
	printf("#viMode: %d, vpss out width: %d, out height: %d\n", stTestCtx.viMode, pVpssCfg->u32Width, pVpssCfg->u32Height);
	printf("#vpss group crop enabled : %d, chn crop enabled : %d\n", pVpssCfg->bGrpCropEn ? 1 : 0, pVpssCfg->bChnCropEn ? 1 : 0);
	printf("#vpss group crop ratio : %d, chn crop ratio : %d\n", pVpssCfg->s32GrpCropRatio, pVpssCfg->s32ChnCropRatio);
	printf("#vpss group mirror enable : %d, group flip enable : %d\n", pVpssCfg->s32GrpMirror, pVpssCfg->s32GrpFlip);
	printf("#vpss chn mirror enable : %d, chn flip enable : %d\n", pVpssCfg->s32Mirror, pVpssCfg->s32Flip);
	printf("#vpss bWrapEn : %d, u32WrapLine : %d\n", pVpssCfg->bEnableWrap, pVpssCfg->u32WrapLine);
	printf("#vpss dst compressMode: %d\n", pVpssCfg->s32DstCompressMode);
	printf("#venc in width: %d, in height: %d\n", pVencCfg->u32Width, pVencCfg->u32Height);
	printf("#Second resolution: In: %dx%d, Out: %dx%d\n", pViCfg1->u32InWidth, pViCfg1->u32InHeight, pViCfg1->u32OutWidth, pViCfg1->u32OutHeight);
	printf("#Second vi bWrapEn: %d, u32WrapLine: %d\n", pViCfg1->bEnableWrap, pViCfg1->u32WrapLine);
	printf("#enCodecType: %d, s32FrameCnt: %d, bitrate: %d\n", pVencCfg->enCodecType, g_s32FrameCnt, pVencCfg->u32Bitrate);
	printf("#Second vpss bWrapEn : %d, u32WrapLine : %d\n", pVpssCfg1->bEnableWrap, pVpssCfg1->u32WrapLine);
	printf("#Second venc in width: %d, in height: %d\n", pVencCfg1->u32Width, pVencCfg1->u32Height);

	signal(SIGINT, sigterm_handler);

	if (pIqfilesPath) {
#ifdef RKAIQ
		RK_BOOL bMultiCam = RK_FALSE;

		if (stTestCtx.u32CamNum == 2)
			bMultiCam = RK_TRUE;

		printf("ISP IQ file path: %s\n", pIqfilesPath);
		if (stTestCtx.firstMode == TEST_ONLINE_MODE) {
			SIMPLE_COMM_ISP_Init(pViCfg->u32PipeId, RK_AIQ_WORKING_MODE_NORMAL, bMultiCam, pIqfilesPath);
			SIMPLE_COMM_ISP_Run(pViCfg->u32PipeId);
		}

		if ((stTestCtx.secondMode == TEST_ONLINE_MODE) && (stTestCtx.u32CamNum == 2)) {
			SIMPLE_COMM_ISP_Init(pViCfg1->u32PipeId, RK_AIQ_WORKING_MODE_NORMAL, bMultiCam, pIqfilesPath);
			SIMPLE_COMM_ISP_Run(pViCfg1->u32PipeId);
		}
#ifdef RV1126B
		if (stTestCtx.firstMode == TEST_ONLINE_MODE) {
			if (rk_aiq_uapi2_judgeNeedRawPipeCtrl(g_aiq_ctx[pViCfg->u32PipeId])) {
				RK_MPI_VI_CtrlRawStream(pViCfg->u32PipeId, false);
				printf("raw stream control by rkaiq\n");
			} else {
				RK_MPI_VI_CtrlRawStream(pViCfg->u32PipeId, true);
				printf("raw stream control by rockit\n");
			}
		}
#endif
#endif
	}

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		printf("rk mpi sys init fail!");

		if (pIqfilesPath) {
			if (stTestCtx.firstMode == TEST_ONLINE_MODE)
				SIMPLE_COMM_ISP_Stop(pViCfg->u32PipeId);
			if ((stTestCtx.secondMode == TEST_ONLINE_MODE) && (stTestCtx.u32CamNum == 2))
				SIMPLE_COMM_ISP_Stop(pViCfg1->u32PipeId);
		}

		return -1;
	}

#ifdef RV1126B
	VI_PARAM_MOD_S stModParam;
	/* test for VI_EXT_CHN_MODE */
	memset(&stModParam, 0, sizeof(stModParam));
	stModParam.enViModType = VI_EXT_CHN_MODE;
	// stModParam.stExtChnParam.mirrorCmsc = 1;
	// stModParam.stExtChnParam.extChn[0] = 1;
	s32Ret = RK_MPI_VI_SetModParam(&stModParam);
	if (s32Ret)
	RK_LOGE("RK_MPI_VI_SetModParam fail:%#X", s32Ret);

	memset(&stModParam, 0, sizeof(stModParam));
	stModParam.enViModType = VI_EXT_CHN_MODE;
	s32Ret = RK_MPI_VI_GetModParam(&stModParam);
	if (s32Ret)
	RK_LOGE("RK_MPI_VI_GetModParam fail:%#X", s32Ret);

	RK_LOGI("vi mod:%d mirror:%d ext_chn_mode:%d ext_chn1_mode:%d ext_chn2_mode:%d ext_chn3_mode:%d",
	stModParam.enViModType, stModParam.stExtChnParam.mirrorCmsc,
	stModParam.stExtChnParam.extChn[0], stModParam.stExtChnParam.extChn[1],
	stModParam.stExtChnParam.extChn[2], stModParam.stExtChnParam.extChn[3]);
#endif

	open_file(stTestCtx.u32CamNum, pfileDir, bSaveFile, pinFile1, pinFile2, pinFile3, pinFile4, outFileDir, outFileDir1);
	init_all_vi_wrap(stTestCtx.u32CamNum);
	if (stTestCtx.viMode == TEST_VI_MODE_VI_ONLY) {
		pthread_create(&tid, NULL, get_vi_frame, &pViCfg->u32PipeId);
		if (stTestCtx.firstMode == TEST_OFFLINE_MODE) {
			ret = pthread_create(&pViCfg->tid, NULL, send_raw_frame, pViCfg);
			if (ret) {
				printf("Create send raw frame(%d) thread failed %d\n", pViCfg->u32ChnId, ret);
				goto __FAILED;
			}
		}
	} else if (stTestCtx.viMode == TEST_VI_MODE_BIND_VPSS) {
		init_vpss(pViCfg, pVpssCfg, stTestCtx.firstMode);
	} else if (stTestCtx.viMode == TEST_VI_MODE_BIND_VPSS_BIND_VENC) {
		if (init_venc_wrap(pViCfg, pVencCfg, pVpssCfg, stTestCtx.firstMode))
			return -1;

		if (stTestCtx.u32CamNum == 2)
			init_venc_wrap(pViCfg1, pVencCfg1, pVpssCfg1, stTestCtx.secondMode);
	} else if (stTestCtx.viMode == TEST_VI_MODE_BIND_VENC) {
		if (init_vi_venc_wrap(pViCfg, pVencCfg, stTestCtx.firstMode))
			return -1;

		if (stTestCtx.u32CamNum == 2) {
			if (init_vi_venc_wrap(pViCfg1, pVencCfg1, stTestCtx.secondMode))
				return -1;
		}
	} else {
		printf("viMode(%d) set failed", stTestCtx.viMode);
		return -1;
	}

	if (stTestCtx.viMode == TEST_VI_MODE_VI_ONLY) {
		if (pViCfg->tid) {
			ret = pthread_join(pViCfg->tid, NULL);
			if (ret)
				printf("Exit send vi[%d] thread failed!\n", pViCfg->u32ChnId);
			else
				printf("Exit send vi[%d] thread ok\n", pViCfg->u32ChnId);
			tid = 0;
		}

		if (tid) {
			ret = pthread_join(tid, NULL);
			if (ret)
				printf("Exit get vi[%d] thread failed!\n", pViCfg->u32ChnId);
			else
				printf("Exit get vi[%d] thread ok\n", pViCfg->u32ChnId);
			tid = 0;
		}
	} else if (stTestCtx.viMode == TEST_VI_MODE_BIND_VPSS) {
		deinit_vpss(pViCfg, pVpssCfg);
	} else if (stTestCtx.viMode == TEST_VI_MODE_BIND_VPSS_BIND_VENC) {
		if (stTestCtx.u32CamNum == 2)
			deinit_venc_wrap(pViCfg1, pVencCfg1, pVpssCfg1, stTestCtx.secondMode);

		deinit_venc_wrap(pViCfg, pVencCfg, pVpssCfg, stTestCtx.firstMode);
	} else if (stTestCtx.viMode == TEST_VI_MODE_BIND_VENC) {
		if (stTestCtx.u32CamNum == 2)
			deinit_vi_venc_wrap(pViCfg1, pVencCfg1, stTestCtx.secondMode);

		deinit_vi_venc_wrap(pViCfg, pVencCfg, stTestCtx.firstMode);
	} else {
		printf("viMode(%d) set failed", stTestCtx.viMode);
		return -1;
	}

	deinit_all_vi_wrap(stTestCtx.u32CamNum);
	close_file(stTestCtx.u32CamNum);

	if (pIqfilesPath) {
		if (stTestCtx.firstMode == TEST_ONLINE_MODE)
			SIMPLE_COMM_ISP_Stop(pViCfg->u32PipeId);
		if ((stTestCtx.secondMode == TEST_ONLINE_MODE) && (stTestCtx.u32CamNum == 2))
			SIMPLE_COMM_ISP_Stop(pViCfg1->u32PipeId);
	}

	printf("RK_MPI_SYS_Exit start\n");
	system("cat /dev/mpi/valloc");
	system("cat /dev/mpi/vsys");
	RK_MPI_SYS_Exit();
	printf("RK_MPI_SYS_Exit end\n");
	system("cat /dev/mpi/valloc");
	system("cat /dev/mpi/vsys");
	printf("Exit main\n");
	return 0;
__FAILED:

	RK_MPI_SYS_Exit();
	printf("error Exit main\n");
	return -1;
}
