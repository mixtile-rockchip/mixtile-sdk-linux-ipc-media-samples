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

#include "rk_irfpa_simple_api.h"
#include "rtsp_demo.h"

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

#include <stdatomic.h>
static atomic_int g_sof_cnt = 0;
static atomic_bool g_should_quit = false;

static int get_raw = 0;
static RK_S32 g_s32FrameCnt = -1;
static RK_U32 g_u32Bitrate = 10 * 1024;
static RK_U32 g_u32SliceSplit = 0;
static bool quit = false;

rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;

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

	VENC_STREAM_S stFrame;
	stFrame.pstPack = malloc(sizeof(VENC_PACK_S));

	while (!quit) {
		s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 200000);
		if (s32Ret == RK_SUCCESS) {
			if (g_rtsplive && g_rtsp_session) {
				pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				rtsp_tx_video(g_rtsp_session, pData, stFrame.pstPack->u32Len,
				              stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
			}

			RK_U64 nowUs = TEST_COMM_GetNowUs();

			RK_LOGD("chn:0, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n",
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

static RK_S32 test_venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
	printf("========%s========\n", __func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

	if (enType == RK_VIDEO_ID_AVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		stAttr.stRcAttr.stH264Cbr.u32BitRate = g_u32Bitrate;
		stAttr.stRcAttr.stH264Cbr.u32Gop = 60;
	} else if (enType == RK_VIDEO_ID_HEVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		stAttr.stRcAttr.stH265Cbr.u32BitRate = g_u32Bitrate;
		stAttr.stRcAttr.stH265Cbr.u32Gop = 60;
	} else if (enType == RK_VIDEO_ID_MJPEG) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
		stAttr.stRcAttr.stMjpegCbr.u32BitRate = g_u32Bitrate;
	}

	stAttr.stVencAttr.enType = enType;
	stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	if (enType == RK_VIDEO_ID_AVC)
		stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
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

	return 0;
}

static void *GetViBuffer(void *arg) {
	(void)arg;
	printf("========%s========\n", __func__);
	void *pData = RK_NULL;
	int loopCount = 0;
	int s32Ret;
	VIDEO_FRAME_INFO_S stViFrame;

	while (!quit) {
		s32Ret = RK_MPI_VI_GetChnFrame(VI_VIR_PIPE_ID, 0, &stViFrame, 1000);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VI_GetChnFrame fail %x\n", s32Ret);
			sleep(1);
			continue;
		}
		loopCount++;
		void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
		RK_LOGD("cnt is %d, vi addr is %p", loopCount, data);
		FILE *fp = fopen("/tmp/y14.raw", "wb");
		fwrite(data, 1, stViFrame.stVFrame.u32Width * stViFrame.stVFrame.u32Height * 2,
		       fp);
		fflush(fp);
		fclose(fp);
		// exit(0);

		s32Ret = RK_MPI_VI_ReleaseChnFrame(VI_VIR_PIPE_ID, 0, &stViFrame);
		if (s32Ret != RK_SUCCESS)
			RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x\n", s32Ret);
	}
	printf("\n======exit %s=======\n", __func__);

	return NULL;
}

int vi_chn_init(int channelId, int width, int height) {
	int ret;
	int buf_cnt = 2;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType =
	    VI_V4L2_MEMORY_TYPE_MMAP; // VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	if (get_raw) {
		vi_chn_attr.enPixelFormat = RK_FMT_YUV400SP_14BIT;
		vi_chn_attr.u32Depth = 1;
		memcpy(vi_chn_attr.stIspOpt.aEntityName, "/dev/video4", strlen("/dev/video4")); // stream_cif_dvp_id0
	} else {
		vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
		vi_chn_attr.u32Depth = 0;
		memcpy(vi_chn_attr.stIspOpt.aEntityName, "/dev/video2", strlen("/dev/video2")); // rk-irfpaisp-result
	}
	ret = RK_MPI_VI_SetChnAttr(VI_VIR_PIPE_ID, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(VI_VIR_PIPE_ID, channelId);
	if (ret) {
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	return ret;
}

static RK_CHAR optstr[] = "?::a:w:h:c:I:e:b:s:";
static void print_usage(const RK_CHAR *name) {
	printf("Usage example:\n");
	printf("\t%s -I 0 -w 400 -h 300 (rtsp://ip/live/0)\n", name);
	printf("\t-a | 0 is nv12→venc→rtsp, 1 is get y14 raw, Default:0\n");
	printf("\t-w | --width: VI width, Default:400\n");
	printf("\t-h | --heght: VI height, Default:300\n");
	printf("\t-c | --frame_cnt: frame number of output, Default:-1\n");
	printf("\t-I | --chnid: camera 0 chn id, Default 0. "
	       "0:rkisp_mainpath, 1:rkisp_selfpath\n");
	printf("\t-e | --encode: encode type, Default:h264, Value:h264, h265\n");
	printf("\t-b | --bitrate: set bitrate (Kbps), Default: 10Mbps\n");
	printf("\t-s | --slice_split: set slice split (0, 1), Default: 0\n");
}

int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_U32 u32Width = 400;
	RK_U32 u32Height = 300;
	// RK_CHAR *pOutPath = NULL;
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	RK_CHAR *pCodecName = "H264";
	RK_S32 s32chnlId = 0;
	int c;
	int ret = -1;

	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'a':
			get_raw = atoi(optarg);
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

	rk_irfpa_simple_api_ctx_t *ctx;
	if (!get_raw) {
		ctx = rk_irfpa_simple_api_create_ctx();
		ctx->cfg.ooc_media = "/dev/media0";
		ctx->cfg.vicap_media = "/dev/media1";
		ctx->cfg.isp_media = "/dev/media3";
		ctx->cfg.iqfile = "/etc/iqfiles/irfpaisp12.json";
		ctx->cfg.log_mode = RK_IRFPA_SIMPLE_API_LOG_DEBUG;
		ctx->cfg.isp_mode = RK_IRFPA_SIMPLE_API_HW_ISP;
		ret = rk_irfpa_simple_api_init(ctx);
		if (!ret) {
			return -1;
		}

		rk_irfpa_simple_api_start(ctx);
	}

	// init rtsp
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
	if (enCodecType == RK_VIDEO_ID_AVC) {
		rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	} else if (enCodecType == RK_VIDEO_ID_HEVC) {
		rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
	} else {
		printf("not support other type\n");
		return -1;
	}
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED;
	}

	vi_chn_init(s32chnlId, u32Width, u32Height);

	// venc  init
	test_venc_init(0, u32Width, u32Height,
	               enCodecType); // RK_VIDEO_ID_AVC RK_VIDEO_ID_HEVC

	MPP_CHN_S stSrcChn, stDestChn;
	// bind vi to venc
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = VI_VIR_PIPE_ID;
	stSrcChn.s32ChnId = s32chnlId;

	stDestChn.enModId = RK_ID_VENC;
	stDestChn.s32DevId = 0;
	stDestChn.s32ChnId = 0;

	pthread_t main_thread;
	if (!get_raw) {
		printf("====RK_MPI_SYS_Bind vi0 to venc0====\n");
		s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("bind 0 ch venc failed");
			goto __FAILED;
		}
		pthread_create(&main_thread, NULL, GetMediaBuffer0, NULL);
	} else {
		pthread_create(&main_thread, NULL, GetViBuffer, NULL);
	}

	while (!quit) {
		usleep(500000);
	}
	pthread_join(main_thread, NULL);

	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);

	if (!get_raw) {
		s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
		}
	}

	s32Ret = RK_MPI_VI_DisableChn(VI_VIR_PIPE_ID, s32chnlId);
	RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

	s32Ret = RK_MPI_VENC_StopRecvFrame(0);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	s32Ret = RK_MPI_VENC_DestroyChn(0);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
	}

	ret = 0;
__FAILED:
	RK_LOGE("test running exit:%d", s32Ret);
	RK_MPI_SYS_Exit();

	if (!get_raw) {
		rk_irfpa_simple_api_stop(ctx);
		rk_irfpa_simple_api_deinit(ctx);
	}

	return ret;
}
