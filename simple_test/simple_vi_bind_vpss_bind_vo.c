#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

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

static FILE *venc0_file;
static bool quit = false;
static RK_S32 g_s32FrameCnt = -1;
#ifdef RV1126B
static	int VoLayer = 0;
static	int VoDev = 0;
static	int VoChn = 0;
static	int IntfType = 1;
static	int enIntfType = 0;
static  RK_U32 disp_width = 1080;
static  RK_U32 disp_height = 1920;
#elif RV1106_RV1103
static	int VoLayer = 0;
static	int VoDev = 0;
static	int VoChn = 0;
static	int IntfType = 0;
static	int enIntfType = 0;
static  RK_U32 disp_width = 320;
static  RK_U32 disp_height = 480;
#elif RK3588
static	int VoLayer = 7;
static	int VoDev = 3;
static	int VoChn = 0;
static	int IntfType = 1;
static	int enIntfType = 0;
static  RK_U32 disp_width = 1080;
static  RK_U32 disp_height = 1920;
#else
static	int VoLayer = 5;
static	int VoDev = 1;
static	int VoChn = 0;
static	int IntfType = 1;
static	int enIntfType = 0;
static  RK_U32 disp_width = 1080;
static  RK_U32 disp_height = 1920;
#endif

static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
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

static int vo_init(int VoLayer, int VoDev, int VoChn, int Width, int Height, int IntfType) {
	int ret = RK_SUCCESS;
	VO_PUB_ATTR_S            stVoPubAttr;
	VO_VIDEO_LAYER_ATTR_S    stLayerAttr;
	VO_CHN_ATTR_S            stChnAttr;

	ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_GRAPHIC);
	if (ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VO_BindLayer failed, ret = %x", ret);
		return ret;
	}

	memset(&stVoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&stChnAttr, 0, sizeof(VO_CHN_ATTR_S));

	if (IntfType != VO_INTF_DEFAULT)
		stVoPubAttr.enIntfType = IntfType;
	else
		stVoPubAttr.enIntfType = VO_INTF_DEFAULT;
	stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

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

	/* Enable Layer */
	stLayerAttr.enPixFormat      = RK_FMT_RGB888;
	stLayerAttr.enCompressMode   = COMPRESS_AFBC_16x16;
	stLayerAttr.stDispRect.s32X  = 0;
	stLayerAttr.stDispRect.s32Y  = 0;
	stLayerAttr.stDispRect.u32Width   = Width;
	stLayerAttr.stDispRect.u32Height  = Height;
	stLayerAttr.stImageSize.u32Width  = Width;
	stLayerAttr.stImageSize.u32Height = Height;
	stLayerAttr.u32DispFrmRt          = 25;

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

	stChnAttr.stRect.s32X = 0;
	stChnAttr.stRect.s32Y = 0;
	stChnAttr.stRect.u32Width = Width;
	stChnAttr.stRect.u32Height = Height;
	stChnAttr.u32FgAlpha = 255;
	stChnAttr.u32BgAlpha = 0;
	stChnAttr.enMirror = MIRROR_NONE;
	stChnAttr.enRotation = ROTATION_0;
	stChnAttr.u32Priority = 1;

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

	ret = RK_MPI_VO_DisableChn(VoLayer, VoChn);
	if (ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VO_DisableChn failed, ret = %x", ret);
		return ret;
	}

	ret = RK_MPI_VO_DisableLayer(VoLayer);
	if (ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VO_DisableLayer failed, ret = %x", ret);
		return ret;
	}

	ret = RK_MPI_VO_Disable(VoDev);
	if (ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VO_Disable failed, ret = %x", ret);
		return ret;
	}

	ret = RK_MPI_VO_UnBindLayer(VoLayer, VoDev);
	if (ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VO_UnBindLayer failed, ret = %x", ret);
		return ret;
	}

	RK_MPI_VO_CloseFd();

	RK_LOGE("Destroy vo [dev: %d, layer: %d, chn: %d] success!",
			VoDev, VoLayer, VoChn);
	return ret;
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
	printf("================================%s==================================\n",
	       __func__);
	int ret;
	int buf_cnt = 5;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType =
	    VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 0; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
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
#ifdef RV1126B
	stGrpVpssAttr.enVProcDev = VIDEO_PROC_DEV_RGA; //1126b bind need use rga
#endif

	stVpssChnAttr.enChnMode = VPSS_CHN_MODE_USER;
	stVpssChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
	stVpssChnAttr.enPixelFormat = RK_FMT_YUV420SP;
	stVpssChnAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssChnAttr.stFrameRate.s32DstFrameRate = -1;
	stVpssChnAttr.u32Width = width;
	stVpssChnAttr.u32Height = height;
	stVpssChnAttr.enCompressMode = COMPRESS_MODE_NONE;
	stVpssChnAttr.u32FrameBufCnt = 8;

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

	s32Ret = RK_MPI_VPSS_StartGrp(s32Grp);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	return s32Ret;
}

static RK_CHAR optstr[] = "?::W:H:w:h:c:I:e:o:l:d:t:";

static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -I 0 -W 1920 -H 1080 -w 1280 -h 720 -e h264 -o /tmp/venc.h264\n", name);
	printf("\t-W | --width: VI width, Default:1920\n");
	printf("\t-H | --heght: VI height, Default:1080\n");
	printf("\t-w | --disp_width: VPSS width, Default:1280\n");
	printf("\t-h | --disp_height: VPSS height, Default:720\n");
	printf("\t-c | --frame_cnt: frame number of output, Default:-1\n");
#if defined RV1126_RV1109
	printf("\t-I | --chnid: camera 0 chn id, Default 0. "
			"0:rkisp_m_bypass, 1:rkispp_scale0, 2:rkispp_scale2, 2:rkispp_scale3\n");
#elif (defined RV1106_RV1103 || defined RV1103B) || defined(RV1126B)
	printf("\t-I | --chnid: camera 0 chn id, Default 0. "
			"0:rkisp_mainpath, 1:rkisp_selfpath, 2:rkisp_bypasspath\n");
#endif
	printf("\t-e | --encode: encode type, Default:h264, Value:h264, h265, mjpeg\n");
	printf("\t-o: output path, Default:NULL\n");
	printf("\t-l | --layer: Vo layer, Default 0. \n");
	printf("\t-d | --device: Vo device, Default 0. \n");
	printf("\t-t | --type: Vo en intf type, Default 0. \n");
}

int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	MPP_CHN_S stSrcChn, stvpssChn, stvoChn;

	RK_U32 u32Width = 1920;
	RK_U32 u32Height = 1080;

	RK_CHAR *pOutPath = NULL;
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	RK_CHAR *pCodecName = "H264";
	RK_S32 s32chnlId = 0;
	int c;
	int ret = -1;

	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'W':
			u32Width = atoi(optarg);
			break;
		case 'H':
			u32Height = atoi(optarg);
			break;
		case 'w':
			disp_width = atoi(optarg);
			break;
		case 'h':
			disp_height = atoi(optarg);
			break;
		case 'I':
			s32chnlId = atoi(optarg);
			break;
		case 'c':
			g_s32FrameCnt = atoi(optarg);
			break;
		case 'o':
			pOutPath = optarg;
			break;
		case 'l':
			VoLayer = atoi(optarg);
			break;
		case 'd':
			VoDev = atoi(optarg);
			break;
		case 't':
			IntfType = atoi(optarg);
			break;
		case '?':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	printf("#CodecName:%s, enCodecType: %d\n", pCodecName, enCodecType);
	printf("#INPUT Resolution: %dx%d\n", u32Width, u32Height);
	printf("#VPSS Resolution: %dx%d\n", disp_width, disp_height);
	printf("#Output Path: %s\n", pOutPath);
	printf("#ChannelIdx: %d\n\n", s32chnlId);
	printf("#Frame Count to save: %d\n", g_s32FrameCnt);

	if (pOutPath) {
		venc0_file = fopen(pOutPath, "w");
		if (!venc0_file) {
			printf("ERROR: open file: %s fail, exit\n", pOutPath);
			return 0;
		}
	}
	signal(SIGINT, sigterm_handler);

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED;
	}

	vi_dev_init();
	vi_chn_init(s32chnlId, u32Width, u32Height);
	test_vpss_init(0, disp_width, disp_height);
	// vo  init
	enIntfType = vo_get_en_intf_type(IntfType);
	vo_init(VoLayer, VoDev, VoChn, disp_width, disp_height, enIntfType);

	// bind vi to vpss
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = s32chnlId;

	stvpssChn.enModId = RK_ID_VPSS;
	stvpssChn.s32DevId = 0;
	stvpssChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vi0 to vpss0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch venc failed");
		goto __FAILED;
	}

	stvoChn.enModId = RK_ID_VO;
	stvoChn.s32DevId = VoLayer;
	stvoChn.s32ChnId = VoChn;
	printf("====RK_MPI_SYS_Bind vpss0 to vo====\n");
	s32Ret = RK_MPI_SYS_Bind(&stvpssChn, &stvoChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 1 ch venc failed");
		goto __FAILED;
	}

	// pthread_t main_thread;
	// pthread_create(&main_thread, NULL, GetMediaBuffer0, NULL);

	while (!quit) {
		usleep(50000);
	}
	// pthread_join(main_thread, NULL);
	ret = 0;
__FAILED:

	s32Ret = RK_MPI_SYS_UnBind(&stvpssChn, &stvoChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
	}

	s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
	}

	s32Ret = RK_MPI_VI_DisableChn(0, s32chnlId);
	RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

	RK_MPI_VPSS_StopGrp(0);
	RK_MPI_VPSS_DestroyGrp(0);

	vo_deinit(VoLayer, VoDev, VoChn);

	s32Ret = RK_MPI_VI_DisableDev(0);
	RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);

	RK_LOGE("test running exit:%d", s32Ret);
	RK_MPI_SYS_Exit();

	return ret;
}
