// Description:
// - 8-channel VI input, each VI channel binds to one VPSS group for 8-grid display
// - VI part reuses the approach from simple_vi_avs_send_vo.c: using virtual pipe, s32PipeId=VI_MAX_PHY_PIPE_NUM
// - Save one frame from each VI channel to /data/simple_multi_ahd_vpss_test/
// - 8 VPSS groups, each group has 4 VPSS channels:
//   VPSS group i ch0: Same resolution as input, encode and save to /data/simple_multi_ahd_vpss_test/venc_grp{i}_main.h264
//   VPSS group i ch1: 704x576 encode and save to /data/simple_multi_ahd_vpss_test/venc_grp{i}_sub.h264
//   VPSS group i ch2: Same resolution as input, save 1 frame to /data/simple_multi_ahd_vpss_test/vpss_grp{i}_ch2_0.yuv
//   VPSS group i ch3: Quad display size(g_disp_w/2 x g_disp_h/2), bind to VO channel i for display
// - Support extension to 8 VI + 8 VPSS + 8-grid display

/*
====================================================================
VPSS Channel Function Assignment:
VPSS Group 0: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO0)
VPSS Group 1: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO1)
VPSS Group 2: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO2)
VPSS Group 3: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO3)
VPSS Group 4: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO4)
VPSS Group 5: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO5)
VPSS Group 6: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO6)
VPSS Group 7: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO7)

Grid Layout Example:
┌─────────┬─────────┬─────────┬─────────┐
│   VI0   │   VI1   │   VI2   │   VI3   │
│ (VPSS0) │ (VPSS1) │ (VPSS2) │ (VPSS3) │
├─────────┼─────────┼─────────┼─────────┤
│   VI4   │   VI5   │   VI6   │   VI7   │
│ (VPSS4) │ (VPSS5) │ (VPSS6) │ (VPSS7) │
└─────────┴─────────┴─────────┴─────────┘

Data Storage:
/data/simple_multi_ahd_vpss_test/
├── vi_ch0_0.yuv          # VI0 raw data
├── vi_ch1_0.yuv          # VI1 raw data
├── vi_ch2_0.yuv          # VI2 raw data
├── vi_ch3_0.yuv          # VI3 raw data
├── vi_ch4_0.yuv          # VI4 raw data
├── vi_ch5_0.yuv          # VI5 raw data
├── vi_ch6_0.yuv          # VI6 raw data
├── vi_ch7_0.yuv          # VI7 raw data
├── vpss_grp0_ch2_0.yuv   # VPSS group 0 processed data
├── vpss_grp1_ch2_0.yuv   # VPSS group 1 processed data
├── vpss_grp2_ch2_0.yuv   # VPSS group 2 processed data
├── vpss_grp3_ch2_0.yuv   # VPSS group 3 processed data
├── vpss_grp4_ch2_0.yuv   # VPSS group 4 processed data
├── vpss_grp5_ch2_0.yuv   # VPSS group 5 processed data
├── vpss_grp6_ch2_0.yuv   # VPSS group 6 processed data
├── vpss_grp7_ch2_0.yuv   # VPSS group 7 processed data
├── venc_grp0_main.h264   # VPSS group 0 main stream
├── venc_grp0_sub.h264    # VPSS group 0 sub stream
├── venc_grp1_main.h264   # VPSS group 1 main stream
├── venc_grp1_sub.h264    # VPSS group 1 sub stream
├── venc_grp2_main.h264   # VPSS group 2 main stream
├── venc_grp2_sub.h264    # VPSS group 2 sub stream
├── venc_grp3_main.h264   # VPSS group 3 main stream
├── venc_grp3_sub.h264    # VPSS group 3 sub stream
├── venc_grp4_main.h264   # VPSS group 4 main stream
├── venc_grp4_sub.h264    # VPSS group 4 sub stream
├── venc_grp5_main.h264   # VPSS group 5 main stream
├── venc_grp5_sub.h264    # VPSS group 5 sub stream
├── venc_grp6_main.h264   # VPSS group 6 main stream
├── venc_grp6_sub.h264    # VPSS group 6 sub stream
├── venc_grp7_main.h264   # VPSS group 7 main stream
└── venc_grp7_sub.h264    # VPSS group 7 sub stream

*/

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"

// Maximum supported channel definitions
#define MAX_VI_CHN 8     // 支持8路
#define MAX_VPSS_GRP 8   // 支持8组
#define MAX_VO_CHN 8     // 支持8路显示

static volatile RK_BOOL g_quit = RK_FALSE;

// VI input resolution
static RK_U32 g_in_w = 1920;
static RK_U32 g_in_h = 1080;

// VO total display resolution (default portrait 1080x1920)
static RK_U32 g_disp_w = 1080;
static RK_U32 g_disp_h = 1920;

// Use virtual pipe: consistent with simple_vi_avs_send_vo.c
static int s32PipeId = VI_MAX_PHY_PIPE_NUM; // VI pipe ID (virtual)

#ifdef RV1126B
static int g_VoLayer = 0;
static int g_VoDev = 0;
static int g_IntfType = VO_INTF_MIPI;
#elif defined(RV1106_RV1103)
static int g_VoLayer = 0;
static int g_VoDev = 0;
static int g_IntfType = VO_INTF_DEFAULT;
#else
static int g_VoLayer = 5;
static int g_VoDev = 1;
static int g_IntfType = VO_INTF_MIPI;
#endif

// Multi-channel device information structure
typedef struct {
	int chnId;
	char deviceName[64];
	bool isActive;
	int vpssGrp;  // Corresponding VPSS group number
} VI_CHN_INFO;

static VI_CHN_INFO g_chnInfo[MAX_VI_CHN] = {
    {0, "/dev/video1", false, 0},
    {1, "/dev/video2", false, 1},
    {2, "/dev/video3", false, 2},
    {3, "/dev/video4", false, 3},
    {4, "/dev/video5", false, 4},
    {5, "/dev/video6", false, 5},
    {6, "/dev/video7", false, 6},
    {7, "/dev/video8", false, 7},
};

// VPSS thread context
typedef struct {
	RK_S32 grp;
	RK_S32 chn;
	volatile RK_BOOL *pQuit;
} vpss_thread_ctx_t;

// VENC context
typedef struct venc_ctx_s {
	int chn;
	int grp;  // Corresponding VPSS group
	FILE *fp;
	char out_path[256];
	volatile RK_BOOL *pQuit;
} venc_ctx_t;

static void sig_handler(int sig) {
	(void)sig;
	g_quit = RK_TRUE;
}

static RK_BOOL check_device_exists(const char *dev_name) {
	struct stat st;
	return (stat(dev_name, &st) == 0) ? RK_TRUE : RK_FALSE;
}

static void ensure_data_dir(void) {
	system("mkdir -p /data/simple_multi_ahd_vpss_test >/dev/null 2>&1");
}

static int save_yuv_one(const char *path, const VIDEO_FRAME_INFO_S *f) {
	if (!f || !f->stVFrame.pMbBlk)
		return -1;
	void *vir = RK_MPI_MB_Handle2VirAddr(f->stVFrame.pMbBlk);
	RK_U32 len = RK_MPI_MB_GetSize(f->stVFrame.pMbBlk);
	if (!vir || !len)
		return -1;
	FILE *fp = fopen(path, "wb");
	if (!fp)
		return -1;
	size_t w = fwrite(vir, 1, len, fp);
	fclose(fp);
	return (w == len) ? 0 : -1;
}

// ===================================================================
// Grid layout calculation functions
// ===================================================================
typedef struct {
	RK_U32 rows;        // Number of rows
	RK_U32 columns;     // Number of columns
	RK_U32 winWidth;    // Width of each window
	RK_U32 winHeight;   // Height of each window
} GRID_LAYOUT_S;

/**
 * Calculate grid layout for multi-channel video
 * @param numChannels: Number of video channels (1-8)
 * @param totalWidth: Total display width
 * @param totalHeight: Total display height
 * @param layout: Output layout information
 * @return: 0 success, -1 failure
 */
static int calculate_grid_layout(int numChannels, int totalWidth, int totalHeight, GRID_LAYOUT_S *layout) {
	if (!layout || numChannels <= 0 || numChannels > 8) {
		printf("Invalid parameters for grid layout calculation\n");
		return -1;
	}

	// Check if portrait mode (will use 270 degree rotation)
	RK_BOOL isPortrait = (totalWidth == 1080 && totalHeight == 1920);

	// Calculate optimal row/column layout based on channel count
	switch (numChannels) {
	case 1:
		layout->rows = 1;
		layout->columns = 1;
		break;
	case 2:
		if (isPortrait) {
			// Portrait mode: 2 rows 1 column, becomes left-right split after rotation
			layout->rows = 2;
			layout->columns = 1;
		} else {
			// Landscape mode: 1 row 2 columns, direct left-right split
			layout->rows = 1;
			layout->columns = 2;
		}
		break;
	case 3:
	case 4:
		layout->rows = 2;
		layout->columns = 2;
		break;
	case 5:
	case 6:
		if (isPortrait) {
			// Portrait mode: 3 rows 2 columns, becomes 2 rows 3 columns after rotation
			layout->rows = 3;
			layout->columns = 2;
		} else {
			// Landscape mode: 2 rows 3 columns
			layout->rows = 2;
			layout->columns = 3;
		}
		break;
	case 7:
	case 8:
		if (isPortrait) {
			// Portrait mode: 4 rows 2 columns, becomes 2 rows 4 columns after rotation
			layout->rows = 4;
			layout->columns = 2;
		} else {
			// Landscape mode: 2 rows 4 columns
			layout->rows = 2;
			layout->columns = 4;
		}
		break;
	default:
		printf("Unsupported channel number: %d\n", numChannels);
		return -1;
	}

	// Calculate size of each window
	layout->winWidth = totalWidth / layout->columns;
	layout->winHeight = totalHeight / layout->rows;

	printf("Grid Layout: %d channels -> %dx%d grid, window size: %dx%d%s\n",
	       numChannels, layout->columns, layout->rows,
	       layout->winWidth, layout->winHeight,
	       isPortrait ? " (Portrait mode with 270° rotation)" : "");

	return 0;
}

/**
 * Calculate window position based on channel index
 * @param channelIndex: Channel index (starting from 0)
 * @param layout: Layout information
 * @param x: Output X coordinate
 * @param y: Output Y coordinate
 */
static void calculate_window_position(int channelIndex, const GRID_LAYOUT_S *layout, RK_U32 *x, RK_U32 *y) {
	*x = (channelIndex % layout->columns) * layout->winWidth;
	*y = (channelIndex / layout->columns) * layout->winHeight;
}

// VI channel initialization
static int vi_chn_init(int channelId, const char *video_dev, int width, int height) {
	int ret;
	VI_CHN_ATTR_S stChnAttr;
	memset(&stChnAttr, 0, sizeof(stChnAttr));
	strncpy(stChnAttr.stIspOpt.aEntityName, video_dev,
	        sizeof(stChnAttr.stIspOpt.aEntityName) - 1);
	stChnAttr.stIspOpt.u32BufCount = 8;
	stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
	stChnAttr.stIspOpt.bNoUseLibV4L2 = RK_FALSE;
	stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_MMAP;
	stChnAttr.u32Depth = 4;
	stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
	stChnAttr.stSize.u32Width = width;
	stChnAttr.stSize.u32Height = height;
	stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
	stChnAttr.stFrameRate.s32SrcFrameRate = -1;
	stChnAttr.stFrameRate.s32DstFrameRate = -1;
	ret = RK_MPI_VI_SetChnAttr(s32PipeId, channelId, &stChnAttr);
	if (ret != RK_SUCCESS)
		return ret;
	ret = RK_MPI_VI_EnableChn(s32PipeId, channelId);
	return ret;
}

// VO initialization - Support multi-channel grid display (1/2/4/8 channels)
static int vo_init(int VoLayer, int VoDev, int Width, int Height, int IntfType, int numChannels) {
	int ret = RK_SUCCESS;
	VO_PUB_ATTR_S stVoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CHN_ATTR_S stChnAttr;
	GRID_LAYOUT_S layout;

	ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_VIDEO);
	if (ret != RK_SUCCESS)
		return ret;

	memset(&stVoPubAttr, 0, sizeof(stVoPubAttr));
	memset(&stLayerAttr, 0, sizeof(stLayerAttr));
	memset(&stChnAttr, 0, sizeof(stChnAttr));

	stVoPubAttr.enIntfType = IntfType;
	stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;
	RK_MPI_VO_SetPubAttr(VoDev, &stVoPubAttr);
	RK_MPI_VO_Enable(VoDev);

	stLayerAttr.enPixFormat = RK_FMT_RGB888;
	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = Width;
	stLayerAttr.stDispRect.u32Height = Height;
	stLayerAttr.stImageSize.u32Width = Width;
	stLayerAttr.stImageSize.u32Height = Height;
	stLayerAttr.u32DispFrmRt = 30;
	RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
	RK_MPI_VO_SetLayerSpliceMode(VoLayer, VO_SPLICE_MODE_RGA);
	RK_MPI_VO_EnableLayer(VoLayer);

	// Use dedicated grid layout calculation function
	if (calculate_grid_layout(numChannels, Width, Height, &layout) != 0) {
		printf("Failed to calculate grid layout\n");
		return -1;
	}

	// Configure each VO channel
	for (int i = 0; i < numChannels; i++) {
		RK_U32 winX, winY;
		calculate_window_position(i, &layout, &winX, &winY);

		stChnAttr.stRect.s32X = winX;
		stChnAttr.stRect.s32Y = winY;
		stChnAttr.stRect.u32Width = layout.winWidth;
		stChnAttr.stRect.u32Height = layout.winHeight;
		stChnAttr.u32FgAlpha = 255;
		stChnAttr.u32BgAlpha = 0;
		stChnAttr.enRotation = (Width == 1080 && Height == 1920) ? ROTATION_270 : ROTATION_0;

		printf("VO Channel %d: position(%d,%d), size(%dx%d)\n",
		       i, winX, winY, layout.winWidth, layout.winHeight);

		RK_MPI_VO_SetChnAttr(VoLayer, i, &stChnAttr);
		RK_MPI_VO_EnableChn(VoLayer, i);
	}

	return 0;
}

// VPSS group initialization
static int vpss_group_init(int grp, int inW, int inH, int voW, int voH) {
	int ret;
	VPSS_GRP_ATTR_S g;
	VPSS_CHN_ATTR_S c;

	memset(&g, 0, sizeof(g));
	g.u32MaxW = 4096;
	g.u32MaxH = 4096;
	g.enPixelFormat = RK_FMT_YUV420SP;
	g.stFrameRate.s32SrcFrameRate = -1;
	g.stFrameRate.s32DstFrameRate = -1;
	g.enCompressMode = COMPRESS_MODE_NONE;
#ifdef RV1126B
	//g.enVProcDev = VIDEO_PROC_DEV_RGA;
	g.enVProcDev = VIDEO_PROC_DEV_VPSS;
#endif
	ret = RK_MPI_VPSS_CreateGrp(grp, &g);
	if (ret)
		return ret;

	memset(&c, 0, sizeof(c));
	c.enChnMode = VPSS_CHN_MODE_USER;
	c.enDynamicRange = DYNAMIC_RANGE_SDR8;
	c.enPixelFormat = RK_FMT_YUV420SP;
	c.stFrameRate.s32SrcFrameRate = -1;
	c.stFrameRate.s32DstFrameRate = -1;
	c.enCompressMode = COMPRESS_MODE_NONE;
	c.u32FrameBufCnt = 8;

	// ch0: Bind to VENC main stream, u32Depth=0
	c.u32Width = inW;
	c.u32Height = inH;
	c.u32Depth = 0;
	RK_MPI_VPSS_SetChnAttr(grp, 0, &c);
	RK_MPI_VPSS_EnableChn(grp, 0);

	// ch1: Bind to VENC sub stream, u32Depth=0
	c.u32Width = 704;
	c.u32Height = 576;
	c.u32Depth = 0;
	RK_MPI_VPSS_SetChnAttr(grp, 1, &c);
	RK_MPI_VPSS_EnableChn(grp, 1);

	// ch2: Manually get frame for saving, u32Depth > 0
	c.u32Width = inW;
	c.u32Height = inH;
	c.u32Depth = 2;
	RK_MPI_VPSS_SetChnAttr(grp, 2, &c);
	RK_MPI_VPSS_EnableChn(grp, 2);

	// ch3: Bind to VO, quad grid size, u32Depth=0
	c.u32Width = voW;
	c.u32Height = voH;
	c.u32Depth = 0;
	RK_MPI_VPSS_SetChnAttr(grp, 3, &c);
	RK_MPI_VPSS_EnableChn(grp, 3);

	return RK_MPI_VPSS_StartGrp(grp);
}

// VPSS ch2 data acquisition thread
static void *vpss_ch2_thread(void *arg) {
	vpss_thread_ctx_t *ctx = (vpss_thread_ctx_t *)arg;
	RK_S32 grp = ctx->grp;
	RK_S32 chn = ctx->chn;
	volatile RK_BOOL *pQuit = ctx->pQuit;
	RK_BOOL bSaved = RK_FALSE;
	RK_S32 frameCount = 0;

	printf("VPSS grp%d ch%d thread started\n", grp, chn);

	while (!(*pQuit)) {
		VIDEO_FRAME_INFO_S stFrame;
		memset(&stFrame, 0, sizeof(stFrame));

		RK_S32 ret = RK_MPI_VPSS_GetChnFrame(grp, chn, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			frameCount++;

			// Save the first frame
			if (!bSaved) {
				char path[128];
				snprintf(path, sizeof(path), "/data/simple_multi_ahd_vpss_test/vpss_grp%d_ch%d_0.yuv", grp, chn);
				printf("VPSS grp%d ch%d: Got frame %d, saving to %s\n", grp, chn, frameCount, path);
				save_yuv_one(path, &stFrame);
				bSaved = RK_TRUE;
			}

			RK_MPI_VPSS_ReleaseChnFrame(grp, chn, &stFrame);
		} else if (ret != RK_ERR_VPSS_BUF_EMPTY) {
			printf("VPSS grp%d ch%d: GetChnFrame failed, ret=0x%x\n", grp, chn, ret);
			usleep(100000);
		} else {
			usleep(30000);
		}
	}

	printf("VPSS grp%d ch%d thread exiting, processed %d frames\n", grp, chn, frameCount);
	return NULL;
}

// VENC encoder creation
static int venc_create_start(int chn, int grp, int w, int h, RK_CODEC_ID_E type, venc_ctx_t *ctx,
                             volatile RK_BOOL *quitFlag) {
	VENC_CHN_ATTR_S a;
	memset(&a, 0, sizeof(a));
	VENC_RECV_PIC_PARAM_S r;
	memset(&r, 0, sizeof(r));

	a.stVencAttr.enType = type;
	a.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	a.stVencAttr.u32MaxPicWidth = w;
	a.stVencAttr.u32MaxPicHeight = h;
	a.stVencAttr.u32PicWidth = w;
	a.stVencAttr.u32PicHeight = h;
	a.stVencAttr.u32VirWidth = w;
	a.stVencAttr.u32VirHeight = h;
	a.stVencAttr.u32StreamBufCnt = 5;
	a.stVencAttr.u32BufSize = w * h * 3 / 2;
	a.stRcAttr.enRcMode = (type == RK_VIDEO_ID_HEVC) ? VENC_RC_MODE_H265CBR : VENC_RC_MODE_H264CBR;
	a.stRcAttr.stH264Cbr.u32BitRate = (w * h * 30) / 10;
	a.stGopAttr.u32MaxLtrCount = 1;

	if (RK_MPI_VENC_CreateChn(chn, &a) != RK_SUCCESS)
		return -1;

	r.s32RecvPicNum = -1;
	if (RK_MPI_VENC_StartRecvFrame(chn, &r) != RK_SUCCESS)
		return -1;

	ctx->chn = chn;
	ctx->grp = grp;
	ctx->pQuit = quitFlag;
	ctx->fp = fopen(ctx->out_path, "wb");
	return (ctx->fp ? 0 : -1);
}

// VENC stream retrieval thread
static void *venc_stream_thread(void *arg) {
	venc_ctx_t *c = (venc_ctx_t *)arg;
	VENC_STREAM_S s;
	memset(&s, 0, sizeof(s));
	s.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
	if (!s.pstPack)
		return NULL;

	while (!*(c->pQuit)) {
		if (RK_MPI_VENC_GetStream(c->chn, &s, 1000) == RK_SUCCESS) {
			void *p = RK_MPI_MB_Handle2VirAddr(s.pstPack->pMbBlk);
			RK_U32 l = s.pstPack->u32Len;
			if (p && l && c->fp) {
				fwrite(p, 1, l, c->fp);
				fflush(c->fp);
			}
			RK_MPI_VENC_ReleaseStream(c->chn, &s);
		}
	}
	free(s.pstPack);
	return NULL;
}

static void print_usage(const char *n) {
	printf("usage: %s [-w in_w] [-h in_h] [-W vo_w] [-H vo_h] [-n num_channels]\n", n);
	printf("  -w: VI input width (default: 1920)\n");
	printf("  -h: VI input height (default: 1080)\n");
	printf("  -W: VO display width (default: 1080)\n");
	printf("  -H: VO display height (default: 1920)\n");
	printf("  -n: Number of VI/VPSS channels (1-8, default: 4)\n");
	printf("\nSupported grid layouts:\n");
	printf("  1 channel  -> 1x1 grid (full screen)\n");
	printf("  2 channels -> 1x2 grid (side by side)\n");
	printf("  3-4 channels -> 2x2 grid (quad view)\n");
	printf("  5-6 channels -> 2x3 grid\n");
	printf("  7-8 channels -> 2x4 grid\n");
	printf("\nExamples:\n");
	printf("  %s -n 1   # Single channel full screen\n", n);
	printf("  %s -n 2   # Dual channel side by side\n", n);
	printf("  %s -n 4   # Quad view (2x2 grid)\n", n);
	printf("  %s -n 8   # Eight channels (2x4 grid)\n", n);
}

int main(int argc, char *argv[]) {
	int c;
	int num_channels = 4;  // Default 4 channels

	while ((c = getopt(argc, argv, "w:h:W:H:n:?")) != -1) {
		switch (c) {
		case 'w':
			g_in_w = atoi(optarg);
			break;
		case 'h':
			g_in_h = atoi(optarg);
			break;
		case 'W':
			g_disp_w = atoi(optarg);
			break;
		case 'H':
			g_disp_h = atoi(optarg);
			break;
		case 'n':
			num_channels = atoi(optarg);
			if (num_channels < 1 || num_channels > MAX_VI_CHN) {
				printf("Invalid channel number. Range: 1-%d\n", MAX_VI_CHN);
				return -1;
			}
			break;
		case '?':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	ensure_data_dir();

	if (RK_MPI_SYS_Init() != RK_SUCCESS)
		return -1;

	// Probe devices and initialize VI
	int active = 0;
	for (int i = 0; i < num_channels; ++i) {
		g_chnInfo[i].isActive = check_device_exists(g_chnInfo[i].deviceName);
		if (g_chnInfo[i].isActive)
			active++;
	}

	printf("Found %d active VI channels\n", active);
	if (active == 0) {
		printf("No VI devices found!\n");
		goto cleanup;
	}

	// 初始化VI通道
	for (int i = 0; i < num_channels; ++i) {
		if (g_chnInfo[i].isActive) {
			if (vi_chn_init(i, g_chnInfo[i].deviceName, g_in_w, g_in_h) != RK_SUCCESS) {
				printf("Failed to init VI channel %d\n", i);
			}
		}
	}

	// Save one frame from each VI channel
	for (int i = 0; i < num_channels; ++i) {
		if (!g_chnInfo[i].isActive)
			continue;
		VIDEO_FRAME_INFO_S f;
		if (RK_MPI_VI_GetChnFrame(s32PipeId, i, &f, 2000) == RK_SUCCESS) {
			char path[128];
			snprintf(path, sizeof(path), "/data/simple_multi_ahd_vpss_test/vi_ch%d_0.yuv", i);
			save_yuv_one(path, &f);
			RK_MPI_VI_ReleaseChnFrame(s32PipeId, i, &f);
		}
	}

	// Calculate grid cell size (using unified layout calculation function)
	GRID_LAYOUT_S voLayout;
	if (calculate_grid_layout(active, g_disp_w, g_disp_h, &voLayout) != 0) {
		printf("Failed to calculate VO grid layout\n");
		goto cleanup;
	}
	RK_U32 vo_unit_w = voLayout.winWidth;
	RK_U32 vo_unit_h = voLayout.winHeight;

	// Initialize VPSS groups
	for (int i = 0; i < num_channels; ++i) {
		if (g_chnInfo[i].isActive) {
			vpss_group_init(i, g_in_w, g_in_h, vo_unit_w, vo_unit_h);
		}
	}

	// Bind VI to VPSS
	for (int i = 0; i < num_channels; ++i) {
		if (!g_chnInfo[i].isActive)
			continue;
		MPP_CHN_S vi = {.enModId = RK_ID_VI, .s32DevId = s32PipeId, .s32ChnId = i};
		MPP_CHN_S vpss = {.enModId = RK_ID_VPSS, .s32DevId = i, .s32ChnId = 0};
		RK_MPI_SYS_Bind(&vi, &vpss);
	}

	// Create encoders and bind
	venc_ctx_t vencCtx[MAX_VPSS_GRP * 2]; // 每组2个编码器（主/子）
	pthread_t vencThreads[MAX_VPSS_GRP * 2];
	int venc_count = 0;

	for (int i = 0; i < num_channels; ++i) {
		if (!g_chnInfo[i].isActive)
			continue;

		// Main stream encoder
		memset(&vencCtx[venc_count], 0, sizeof(vencCtx[venc_count]));
		snprintf(vencCtx[venc_count].out_path, sizeof(vencCtx[venc_count].out_path),
		         "/data/simple_multi_ahd_vpss_test/venc_grp%d_main.h264", i);
		venc_create_start(venc_count, i, g_in_w, g_in_h, RK_VIDEO_ID_AVC, &vencCtx[venc_count], &g_quit);

		MPP_CHN_S vpss_main = {.enModId = RK_ID_VPSS, .s32DevId = i, .s32ChnId = 0};
		MPP_CHN_S venc_main = {.enModId = RK_ID_VENC, .s32DevId = 0, .s32ChnId = venc_count};
		RK_MPI_SYS_Bind(&vpss_main, &venc_main);
		pthread_create(&vencThreads[venc_count], NULL, venc_stream_thread, &vencCtx[venc_count]);
		venc_count++;

		// Sub stream encoder
		memset(&vencCtx[venc_count], 0, sizeof(vencCtx[venc_count]));
		snprintf(vencCtx[venc_count].out_path, sizeof(vencCtx[venc_count].out_path),
		         "/data/simple_multi_ahd_vpss_test/venc_grp%d_sub.h264", i);
		venc_create_start(venc_count, i, 704, 576, RK_VIDEO_ID_AVC, &vencCtx[venc_count], &g_quit);

		MPP_CHN_S vpss_sub = {.enModId = RK_ID_VPSS, .s32DevId = i, .s32ChnId = 1};
		MPP_CHN_S venc_sub = {.enModId = RK_ID_VENC, .s32DevId = 0, .s32ChnId = venc_count};
		RK_MPI_SYS_Bind(&vpss_sub, &venc_sub);
		pthread_create(&vencThreads[venc_count], NULL, venc_stream_thread, &vencCtx[venc_count]);
		venc_count++;
	}

	// Initialize VO
	vo_init(g_VoLayer, g_VoDev, g_disp_w, g_disp_h, g_IntfType, active);

	// Bind VPSS ch3 to VO
	for (int i = 0; i < num_channels; ++i) {
		if (!g_chnInfo[i].isActive)
			continue;
		MPP_CHN_S vpss_vo = {.enModId = RK_ID_VPSS, .s32DevId = i, .s32ChnId = 3};
		MPP_CHN_S vo = {.enModId = RK_ID_VO, .s32DevId = g_VoLayer, .s32ChnId = i};
		RK_MPI_SYS_Bind(&vpss_vo, &vo);
	}

	// Start VPSS ch2 threads
	vpss_thread_ctx_t vpssCtx[MAX_VPSS_GRP];
	pthread_t vpssThreads[MAX_VPSS_GRP];
	for (int i = 0; i < num_channels; ++i) {
		if (!g_chnInfo[i].isActive)
			continue;
		vpssCtx[i].grp = i;
		vpssCtx[i].chn = 2;
		vpssCtx[i].pQuit = &g_quit;
		pthread_create(&vpssThreads[i], NULL, vpss_ch2_thread, &vpssCtx[i]);
	}

	printf("System initialized, displaying %dx%d grid (%d channels). Press Ctrl+C to exit.\n",
	       voLayout.columns, voLayout.rows, active);

	while (!g_quit)
		sleep(1);

	printf("Shutting down...\n");

	// Unbind and cleanup
	for (int i = 0; i < num_channels; ++i) {
		if (!g_chnInfo[i].isActive)
			continue;

		MPP_CHN_S vi = {.enModId = RK_ID_VI, .s32DevId = s32PipeId, .s32ChnId = i};
		MPP_CHN_S vpss = {.enModId = RK_ID_VPSS, .s32DevId = i, .s32ChnId = 0};
		MPP_CHN_S vpss_vo = {.enModId = RK_ID_VPSS, .s32DevId = i, .s32ChnId = 3};
		MPP_CHN_S vo = {.enModId = RK_ID_VO, .s32DevId = g_VoLayer, .s32ChnId = i};

		RK_MPI_SYS_UnBind(&vpss_vo, &vo);
		RK_MPI_SYS_UnBind(&vi, &vpss);
	}

	// Wait for threads to exit
	for (int i = 0; i < venc_count; ++i) {
		pthread_join(vencThreads[i], NULL);
		if (vencCtx[i].fp) {
			fclose(vencCtx[i].fp);
		}
		RK_MPI_VENC_StopRecvFrame(i);
		RK_MPI_VENC_DestroyChn(i);
	}

	for (int i = 0; i < num_channels; ++i) {
		if (g_chnInfo[i].isActive) {
			pthread_join(vpssThreads[i], NULL);
		}
	}

	// Close VO
	for (int i = 0; i < active; ++i) {
		RK_MPI_VO_DisableChn(g_VoLayer, i);
	}
	RK_MPI_VO_DisableLayer(g_VoLayer);
	RK_MPI_VO_Disable(g_VoDev);
	RK_MPI_VO_UnBindLayer(g_VoLayer, g_VoDev);
	RK_MPI_VO_CloseFd();

	// Stop and destroy VPSS
	for (int i = 0; i < num_channels; ++i) {
		if (g_chnInfo[i].isActive) {
			RK_MPI_VPSS_StopGrp(i);
			for (int ch = 0; ch < 4; ++ch) {
				RK_MPI_VPSS_DisableChn(i, ch);
			}
			RK_MPI_VPSS_DestroyGrp(i);
		}
	}

	// Close VI channels
	for (int i = 0; i < num_channels; ++i) {
		RK_MPI_VI_DisableChn(s32PipeId, i);
	}

cleanup:
	RK_MPI_SYS_Exit();
	return 0;
}
