#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_avs.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"

// Function declarations
static void signal_handler(int sig);
static RK_BOOL check_device_exists(const char *dev_name);
int vi_chn_init(int channelId, const char *video_dev, int width, int height);
static RK_S32 vi_poll_event(RK_S32 timeoutMsec, RK_S32 fd);
static void save_frame_to_file(VIDEO_FRAME_INFO_S *frame, int channel_idx);
static void save_avs_frame(VIDEO_FRAME_INFO_S *frame);
static void *GetMediaBufferAVS(void *arg);
static void *GetAvsOutput(void *arg);
static int vo_init(int VoLayer, int VoDev, int VoChn, int Width, int Height,
                   int IntfType);
static int vo_deinit(int VoLayer, int VoDev, int VoChn);
static int vo_get_en_intf_type(int type);
static RK_S32 avs_init();
static RK_S32 avs_deinit();
static void print_usage(const RK_CHAR *name);

// Global variables
static volatile RK_BOOL quit = RK_FALSE; // Using volatile for visibility across threads
static int u32Width = 1920;
static int u32Height = 1080;
//The virtual channel no longer requires VI dev initialization. Instead, use VI_MAX_PHY_PIPE_NUM as the pipe for the channel.
static int s32PipeId = VI_MAX_PHY_PIPE_NUM;   // VI pipe ID ,
#ifdef RV1126B
static int VoLayer = 0;     // VO layer
static int VoDev = 0;       // VO device
static int VoChn = 0;       // VO channel
static int IntfType = 1;    // Interface type
static int enIntfType = 0;  // Interface type enum
static int VoWidth = 1080;  // VO width
static int VoHeight = 1920; // VO height
#elif RV1106_RV1103
static int VoLayer = 0;     // VO layer
static int VoDev = 0;       // VO device
static int VoChn = 0;       // VO channel
static int IntfType = 0;    // Interface type
static int enIntfType = 0;  // Interface type enum
static int VoWidth = 320;     // VO width
static int VoHeight = 480;    // VO height
#elif RK3588
static int VoLayer = 7;     // VO layer
static int VoDev = 3;       // VO device
static int VoChn = 0;       // VO channel
static int IntfType = 1;    // Interface type
static int enIntfType = 0;  // Interface type enum
static int VoWidth = 1080;  // VO width
static int VoHeight = 1920; // VO height
#else
static int VoLayer = 5;     // VO layer
static int VoDev = 1;       // VO device
static int VoChn = 0;       // VO channel
static int IntfType = 1;    // Interface type
static int enIntfType = 0;  // Interface type enum
static int VoWidth = 1080;  // VO width
static int VoHeight = 1920; // VO height
#endif

// AVS related parameters
static int AVSGrp = 0;              // AVS group ID
static int AVSChn = 0;              // AVS channel ID
static pthread_t avs_thread_id = 0; // AVS output thread

// Multi-channel device configuration
#define MAX_VI_CHN 4
static pthread_t thread_ids[MAX_VI_CHN] = {0};       // Thread ID array
static RK_S32 vi_fds[MAX_VI_CHN] = {-1, -1, -1, -1}; // VI file descriptor array

// Save frame counter
#define MAX_SAVED_FRAMES 2                          // Maximum frames saved per channel
static int saved_frames[MAX_VI_CHN] = {0, 0, 0, 0}; // Frames saved per channel
static int avs_saved_frames = 0;                    // Frames saved for AVS output
#define AVS_MAX_SAVED_FRAMES 2 // Maximum frames saved per AVS channel

// VI channel information structure
typedef struct {
	int chnId;
	char deviceName[64];
	bool isActive;
} VI_CHN_INFO;

static VI_CHN_INFO chnInfo[MAX_VI_CHN] = {{0, "/dev/video1", false},
                                          {1, "/dev/video2", false},
                                          {2, "/dev/video3", false},
                                          {3, "/dev/video4", false}};

// Signal handler for graceful exit
static void signal_handler(int sig) {
	printf("Got signal %d\n", sig);
	quit = RK_TRUE;
}

// Check if device exists
static RK_BOOL check_device_exists(const char *dev_name) {
	struct stat st;
	if (stat(dev_name, &st) == 0) {
		return RK_TRUE;
	}
	return RK_FALSE;
}


// Initialize VI channel
int vi_chn_init(int channelId, const char *video_dev, int width, int height) {
	int ret;

	// VI channel attributes
	VI_CHN_ATTR_S stChnAttr;
	memset(&stChnAttr, 0, sizeof(stChnAttr));

	// Set entity name
	strncpy(stChnAttr.stIspOpt.aEntityName, video_dev,
	        sizeof(stChnAttr.stIspOpt.aEntityName) - 1);

	// Buffer configuration
	stChnAttr.stIspOpt.u32BufCount = 8;
	stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
	stChnAttr.stIspOpt.bNoUseLibV4L2 = RK_FALSE;
	stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_MMAP;

	// Set queue depth
	stChnAttr.u32Depth = 4;

	// Set image format
	stChnAttr.enPixelFormat = RK_FMT_YUV420SP;
	stChnAttr.stSize.u32Width = width;
	stChnAttr.stSize.u32Height = height;
	stChnAttr.enCompressMode = COMPRESS_MODE_NONE;

	// Frame rate setting, -1 means no change in frame rate
	stChnAttr.stFrameRate.s32SrcFrameRate = -1;
	stChnAttr.stFrameRate.s32DstFrameRate = -1;

	printf("Setting VI channel %d, device: %s, resolution: %dx%d\n", channelId, video_dev,
	       width, height);

	// Set channel attributes
	ret = RK_MPI_VI_SetChnAttr(s32PipeId, channelId, &stChnAttr);
	if (ret != RK_SUCCESS) {
		printf("Error: RK_MPI_VI_SetChnAttr failed: %x\n", ret);
		return ret;
	}

	// Enable channel
	ret = RK_MPI_VI_EnableChn(s32PipeId, channelId);
	if (ret != RK_SUCCESS) {
		printf("Error: RK_MPI_VI_EnableChn failed: %x\n", ret);
		return ret;
	}

	printf("VI channel %d initialized successfully\n", channelId);
	// Get channel file descriptor
	return RK_MPI_VI_GetChnFd(s32PipeId, channelId);
}

// Poll for VI events
static RK_S32 vi_poll_event(RK_S32 timeoutMsec, RK_S32 fd) {
	struct pollfd pollFd;
	RK_S32 ret = 0;

	if (fd < 0) {
		return -1;
	}

	memset(&pollFd, 0, sizeof(pollFd));
	pollFd.fd = fd;
	pollFd.events = POLLIN;

	ret = poll(&pollFd, 1, timeoutMsec);
	if (ret <= 0) {
		return -1;
	}

	if (pollFd.revents & POLLIN) {
		return 0;
	}

	return -1;
}

// Save frame to file for debugging
static void save_frame_to_file(VIDEO_FRAME_INFO_S *frame, int channel_idx) {
	char filename[128];

	snprintf(filename, sizeof(filename), "/tmp/channel%d_frame%d.yuv", channel_idx,
	         saved_frames[channel_idx]);
	printf("Saving frame to %s\n", filename);

	FILE *fp = fopen(filename, "wb");
	if (!fp) {
		printf("Failed to open file %s for writing: %s\n", filename, strerror(errno));
		return;
	}

	void *virt_addr = RK_MPI_MB_Handle2VirAddr(frame->stVFrame.pMbBlk);
	if (!virt_addr) {
		printf("Failed to get virtual address for memory block\n");
		fclose(fp);
		return;
	}

	RK_U32 length = RK_MPI_MB_GetSize(frame->stVFrame.pMbBlk);
	size_t written = fwrite(virt_addr, 1, length, fp);

	if (written != length) {
		printf("Failed to write complete data: wrote %zu of %u bytes\n", written, length);
	} else {
		printf("Successfully saved YUV frame to %s (%u bytes)\n", filename, length);
		saved_frames[channel_idx]++;
	}

	fclose(fp);
}

// Save AVS output frame to file for debugging
static void save_avs_frame(VIDEO_FRAME_INFO_S *frame) {
	char filename[128];

	snprintf(filename, sizeof(filename), "/tmp/avs_frame%d.yuv", avs_saved_frames);
	printf("Saving AVS frame to %s\n", filename);

	FILE *fp = fopen(filename, "wb");
	if (!fp) {
		printf("Failed to open file %s for writing: %s\n", filename, strerror(errno));
		return;
	}

	void *virt_addr = RK_MPI_MB_Handle2VirAddr(frame->stVFrame.pMbBlk);
	if (!virt_addr) {
		printf("Failed to get virtual address for memory block\n");
		fclose(fp);
		return;
	}

	RK_U32 length = RK_MPI_MB_GetSize(frame->stVFrame.pMbBlk);
	size_t written = fwrite(virt_addr, 1, length, fp);

	if (written != length) {
		printf("Failed to write complete data: wrote %zu of %u bytes\n", written, length);
	} else {
		printf("Successfully saved AVS frame to %s (%u bytes)\n", filename, length);
		avs_saved_frames++;
	}

	fclose(fp);
}

// AVS mode media buffer acquisition thread function
static void *GetMediaBufferAVS(void *arg) {
	int channel_idx = *((int *)arg);
	RK_S32 s32Ret;
	RK_S32 waitTime = 1000;
	VIDEO_FRAME_INFO_S stViFrame;
	int frameCount = 0;
	int errorCount = 0;
	int maxErrors = 10;

	printf("Starting input channel %d: device=%s\n", channel_idx,
	       chnInfo[channel_idx].deviceName);

	while (!quit) {
		// Get VI frame
		s32Ret = RK_MPI_VI_GetChnFrame(s32PipeId, channel_idx, &stViFrame, waitTime);
		if (s32Ret == RK_SUCCESS) {
			frameCount++;
			errorCount = 0; // Reset error count

			// Save original video frame
			if (saved_frames[channel_idx] < MAX_SAVED_FRAMES) {
				save_frame_to_file(&stViFrame, channel_idx);
			}

			// Send frame to AVS
			s32Ret = RK_MPI_AVS_SendPipeFrame(AVSGrp, channel_idx, &stViFrame, waitTime);
			if (s32Ret != RK_SUCCESS) {
				printf("Channel %d: RK_MPI_AVS_SendPipeFrame failed: %x\n", channel_idx,
				       s32Ret);
			}

			// Release VI frame
			s32Ret = RK_MPI_VI_ReleaseChnFrame(s32PipeId, channel_idx, &stViFrame);
			if (s32Ret != RK_SUCCESS) {
				printf("Channel %d: Release frame failed: %x\n", channel_idx, s32Ret);
			}
		} else {
			errorCount++;
			printf("Channel %d: Get frame timeout %x, error count: %d/%d\n", channel_idx,
			       s32Ret, errorCount, maxErrors);

			usleep(100000); // 100ms
		}
	}

	printf("Channel %d: Exiting, processed %d frames\n", channel_idx, frameCount);
	return NULL;
}

// AVS output processing thread
static void *GetAvsOutput(void *arg) {
	RK_S32 s32Ret;
	RK_S32 waitTime = 1000;
	VIDEO_FRAME_INFO_S stAvsFrame;
	int frameCount = 0;

	printf("Starting output thread\n");

	while (!quit) {
		// Get processed frame from AVS
		s32Ret = RK_MPI_AVS_GetChnFrame(AVSGrp, AVSChn, &stAvsFrame, waitTime);
		if (s32Ret == RK_SUCCESS) {
			frameCount++;

			// Save stitched frame for debugging
			if (avs_saved_frames < AVS_MAX_SAVED_FRAMES) {
				save_avs_frame(&stAvsFrame);
			}

			if (frameCount % 30 == 0) {
				printf("Compositor: %d frames, Output=%dx%d\n", frameCount,
				       stAvsFrame.stVFrame.u32Width, stAvsFrame.stVFrame.u32Height);
			}

			// Send stitched frame to VO for display
			s32Ret = RK_MPI_VO_SendFrame(VoLayer, VoChn, &stAvsFrame, waitTime);
			if (s32Ret != RK_SUCCESS) {
				printf("Error: VO_SendFrame failed: %x\n", s32Ret);
			}

			// Release AVS frame
			s32Ret = RK_MPI_AVS_ReleaseChnFrame(AVSGrp, AVSChn, &stAvsFrame);
			if (s32Ret != RK_SUCCESS) {
				printf("Error: AVS_ReleaseChnFrame failed: %x\n", s32Ret);
			}
		} else {
			// Get frame timeout, wait for a while before retrying
			usleep(30000); // 30ms
		}
	}

	printf("Compositor thread exiting, processed %d frames\n", frameCount);
	return NULL;
}

// Initialize VO device
static int vo_init(int VoLayer, int VoDev, int VoChn, int Width, int Height,
                   int IntfType) {
	int ret = RK_SUCCESS;
	VO_PUB_ATTR_S stVoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CHN_ATTR_S stChnAttr;

	// Bind layer to device
	ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_VIDEO);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_BindLayer failed, ret = %x\n", ret);
		return ret;
	}

	// Set VO device public attributes
	memset(&stVoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	stVoPubAttr.enIntfType = VO_INTF_MIPI;
	stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

	ret = RK_MPI_VO_SetPubAttr(VoDev, &stVoPubAttr);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_SetPubAttr failed, ret = %x\n", ret);
		return ret;
	}

	// Enable VO device
	ret = RK_MPI_VO_Enable(VoDev);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_Enable failed, ret = %x\n", ret);
		return ret;
	}

	// Set layer attributes
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	stLayerAttr.enPixFormat = RK_FMT_RGB888;
	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = Width;
	stLayerAttr.stDispRect.u32Height = Height;
	stLayerAttr.stImageSize.u32Width = Width;
	stLayerAttr.stImageSize.u32Height = Height;
	stLayerAttr.u32DispFrmRt = 30;

	ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_SetLayerAttr failed, ret = %x\n", ret);
		return ret;
	}

	// Set stitching mode to RGA
	ret = RK_MPI_VO_SetLayerSpliceMode(VoLayer, VO_SPLICE_MODE_RGA);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_SetLayerSpliceMode failed, ret = %x\n", ret);
		// Non-fatal error, continue execution
	}

	// Enable layer
	ret = RK_MPI_VO_EnableLayer(VoLayer);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_EnableLayer failed, ret = %x\n", ret);
		return ret;
	}

	// Set channel attributes
	memset(&stChnAttr, 0, sizeof(VO_CHN_ATTR_S));
	stChnAttr.stRect.s32X = 0;
	stChnAttr.stRect.s32Y = 0;
	stChnAttr.stRect.u32Width = Width;
	stChnAttr.stRect.u32Height = Height;
	stChnAttr.u32FgAlpha = 255;
	stChnAttr.u32BgAlpha = 0;

	// Add rotation setting - Horizontal or vertical display
	if (Width == 1080 && Height == 1920) {
		printf("Portrait display detected (1080x1920), applying ROTATION_270\n");
		stChnAttr.enRotation = ROTATION_270;
	} else {
		printf("Standard display mode, no rotation\n");
		stChnAttr.enRotation = ROTATION_0;
	}

	ret = RK_MPI_VO_SetChnAttr(VoLayer, VoChn, &stChnAttr);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_SetChnAttr failed, ret = %x\n", ret);
		return ret;
	}

	ret = RK_MPI_VO_EnableChn(VoLayer, VoChn);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_EnableChn failed, ret = %x\n", ret);
		return ret;
	}

	printf("Created VO [device: %d, layer: %d, channel: %d] successfully!\n", VoDev,
	       VoLayer, VoChn);
	return ret;
}

// Deinitialize VO device
static int vo_deinit(int VoLayer, int VoDev, int VoChn) {
	int ret = 0;

	ret = RK_MPI_VO_DisableChn(VoLayer, VoChn);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_DisableChn failed, ret = %x\n", ret);
		return ret;
	}

	ret = RK_MPI_VO_DisableLayer(VoLayer);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_DisableLayer failed, ret = %x\n", ret);
		return ret;
	}

	ret = RK_MPI_VO_Disable(VoDev);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_Disable failed, ret = %x\n", ret);
		return ret;
	}

	ret = RK_MPI_VO_UnBindLayer(VoLayer, VoDev);
	if (ret != RK_SUCCESS) {
		printf("RK_MPI_VO_UnBindLayer failed, ret = %x\n", ret);
		return ret;
	}

	RK_MPI_VO_CloseFd();

	printf("Destroyed VO [device: %d, layer: %d, channel: %d] successfully!\n", VoDev,
	       VoLayer, VoChn);
	return ret;
}

// Get VO interface type
static int vo_get_en_intf_type(int type) {
	int enIntfType = 0;

	if (type == 1)
		enIntfType = VO_INTF_MIPI;
	else if (type == 2)
		enIntfType = VO_INTF_HDMI;
	else
		enIntfType = VO_INTF_DEFAULT;

	return enIntfType;
}

// Print usage information
static void print_usage(const RK_CHAR *name) {
	printf("Usage example:\n");
	printf("\t%s -w 1920 -h 1080 -W 1080 -H 1920\n", name);
	printf("\nDescription:\n");
	printf("\tCombines 4 camera inputs into one output\n");
	printf("\nParameters:\n");
	printf("\t-w | --width: VI width, Default: 1920\n");
	printf("\t-h | --height: VI height, Default: 1080\n");
	printf("\t-l | --layer: VO layer, Default: 0\n");
	printf("\t-d | --device: VO device, Default: 0\n");
	printf("\t-t | --type: VO interface type, Default: 1 (1:MIPI 2:HDMI)\n");
	printf("\t-W | --vo_width: VO display width, Default: 1080 (portrait mode)\n");
	printf("\t-H | --vo_height: VO display height, Default: 1920 (portrait mode)\n");
}

// Initialize AVS module
static RK_S32 avs_init() {
	RK_S32 s32Ret = RK_SUCCESS;
	AVS_GRP_ATTR_S stGrpAttr;
	AVS_CHN_ATTR_S stChnAttr;
	AVS_PIPE_ATTR_S stPipeAttr[MAX_VI_CHN];

	printf("Initializing AVS group %d\n", AVSGrp);
	memset(&stGrpAttr, 0, sizeof(AVS_GRP_ATTR_S));

	// Set AVS group attributes - Use four-screen mode, no mixing
	stGrpAttr.enMode = AVS_MODE_NOBLEND_QR; // Four-screen mode
	stGrpAttr.u32PipeNum = MAX_VI_CHN;      // 4 input channels
	stGrpAttr.bSyncPipe = RK_FALSE;         // Different pipe synchronization

	// Set input attributes
	stGrpAttr.stInAttr.enParamSource = AVS_PARAM_SOURCE_CALIB;
	stGrpAttr.stInAttr.stSize.u32Width = u32Width;
	stGrpAttr.stInAttr.stSize.u32Height = u32Height;

	// Set output attributes - Ensure matching with VO input
	stGrpAttr.stOutAttr.stSize.u32Width = VoWidth;
	stGrpAttr.stOutAttr.stSize.u32Height = VoHeight;

	printf("Compositor configuration: Input=%dx%d, Output=%dx%d\n", u32Width, u32Height,
	       VoWidth, VoHeight);

	// Set frame rate control (-1 means no change in frame rate)
	stGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stGrpAttr.stFrameRate.s32DstFrameRate = -1;

	// Create AVS group
	s32Ret = RK_MPI_AVS_CreateGrp(AVSGrp, &stGrpAttr);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_CreateGrp failed with %#x\n", s32Ret);
		return s32Ret;
	}

	// Set attributes for each pipe - Four-screen layout
	for (int i = 0; i < MAX_VI_CHN; i++) {
		memset(&stPipeAttr[i], 0, sizeof(AVS_PIPE_ATTR_S));

		// Calculate each channel's position in four-screen
		RK_S32 s32X = (i % 2) * (VoWidth / 2);
		RK_S32 s32Y = (i / 2) * (VoHeight / 2);

		// Set source rectangle - Use entire input image
		stPipeAttr[i].stSrcRect.s32X = 0;
		stPipeAttr[i].stSrcRect.s32Y = 0;
		stPipeAttr[i].stSrcRect.u32Width = u32Width;
		stPipeAttr[i].stSrcRect.u32Height = u32Height;

		// Set target rectangle - Position in four-screen
		stPipeAttr[i].stDstRect.s32X = s32X;
		stPipeAttr[i].stDstRect.s32Y = s32Y;
		stPipeAttr[i].stDstRect.u32Width = VoWidth / 2;
		stPipeAttr[i].stDstRect.u32Height = VoHeight / 2;

		printf("Channel %d position: (%d,%d) %dx%d\n", i, s32X, s32Y, VoWidth / 2,
		       VoHeight / 2);

		// Set priority
		stPipeAttr[i].u32Priority = i;

		// Set attributes for each pipe
		s32Ret = RK_MPI_AVS_SetPipeAttr(AVSGrp, i, &stPipeAttr[i]);
		if (s32Ret != RK_SUCCESS) {
			printf("RK_MPI_AVS_SetPipeAttr for pipe %d failed with %#x\n", i, s32Ret);
			return s32Ret;
		}
	}

	// Set AVS channel attributes
	memset(&stChnAttr, 0, sizeof(AVS_CHN_ATTR_S));
	stChnAttr.u32Width = VoWidth;
	stChnAttr.u32Height = VoHeight;
	stChnAttr.enCompressMode = COMPRESS_MODE_NONE;
	stChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
	stChnAttr.u32Depth = 2;
	stChnAttr.u32FrameBufCnt = 8;
	stChnAttr.stFrameRate.s32SrcFrameRate = -1;
	stChnAttr.stFrameRate.s32DstFrameRate = -1;

	// Set AVS channel attributes
	s32Ret = RK_MPI_AVS_SetChnAttr(AVSGrp, AVSChn, &stChnAttr);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_SetChnAttr failed with %#x\n", s32Ret);
		return s32Ret;
	}

	// Enable AVS channel
	s32Ret = RK_MPI_AVS_EnableChn(AVSGrp, AVSChn);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_EnableChn failed with %#x\n", s32Ret);
		return s32Ret;
	}

	// Start AVS group
	s32Ret = RK_MPI_AVS_StartGrp(AVSGrp);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_StartGrp failed with %#x\n", s32Ret);
		return s32Ret;
	}

	printf("AVS initialized successfully\n");
	return RK_SUCCESS;
}

// Deinitialize AVS module
static RK_S32 avs_deinit() {
	RK_S32 s32Ret = RK_SUCCESS;

	// Stop AVS group
	s32Ret = RK_MPI_AVS_StopGrp(AVSGrp);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_StopGrp failed with %#x\n", s32Ret);
		return s32Ret;
	}

	// Disable channel
	s32Ret = RK_MPI_AVS_DisableChn(AVSGrp, AVSChn);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_DisableChn failed with %#x\n", s32Ret);
		return s32Ret;
	}

	// Destroy AVS group
	s32Ret = RK_MPI_AVS_DestroyGrp(AVSGrp);
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_AVS_DestroyGrp failed with %#x\n", s32Ret);
		return s32Ret;
	}

	printf("AVS deinitialized successfully\n");
	return RK_SUCCESS;
}

int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	int c;
	int ret = -1;

	// Default value setting - For common portrait display scenario
	// VoWidth = 1080;  // Default portrait display width
	// VoHeight = 1920; // Default portrait display height

	// Ensure /tmp directory exists and is writable
	system("mkdir -p /tmp && chmod 777 /tmp");

	while ((c = getopt(argc, argv, "w:h:l:d:t:W:H:?")) != -1) {
		switch (c) {
		case 'w':
			u32Width = atoi(optarg);
			break;
		case 'h':
			u32Height = atoi(optarg);
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
		case 'W':
			VoWidth = atoi(optarg);
			break;
		case 'H':
			VoHeight = atoi(optarg);
			break;
		case '?':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	printf("\n--- Quad-view Video Compositor ---\n");
	printf("Input: %dx%d (4 cameras)\n", u32Width, u32Height);
	printf("Output: %dx%d \n", VoWidth, VoHeight);
	printf("VO Config: Layer=%d, Device=%d, Interface=%d\n", VoLayer, VoDev, IntfType);

	// Set signal handling for multiple signals
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGQUIT, signal_handler);

	// Initialize RK MPI system
	s32Ret = RK_MPI_SYS_Init();
	if (s32Ret != RK_SUCCESS) {
		printf("RK_MPI_SYS_Init failed: %x\n", s32Ret);
		return s32Ret;
	}

	// Detect all available video devices
	int active_channels = 0;
	for (int i = 0; i < MAX_VI_CHN; i++) {
		if (!chnInfo[i].isActive) {
			chnInfo[i].isActive = check_device_exists(chnInfo[i].deviceName);
		}

		if (chnInfo[i].isActive) {
			active_channels++;
		}
	}

	if (active_channels == 0) {
		printf("No active video devices found. Exiting.\n");
		goto __FAILED;
	}

	printf("Found %d active camera devices\n", active_channels);

	// Initialize AVS
	ret = avs_init();
	if (ret != RK_SUCCESS) {
		printf("AVS initialization failed: %x\n", ret);
		goto __FAILED;
	}

	// Initialize VO
	enIntfType = vo_get_en_intf_type(IntfType);
	ret = vo_init(VoLayer, VoDev, VoChn, VoWidth, VoHeight, enIntfType);
	if (ret != RK_SUCCESS) {
		printf("VO initialization failed: %x\n", ret);
		goto __FAILED;
	}

	// Create thread to process AVS output and display to VO
	ret = pthread_create(&avs_thread_id, NULL, GetAvsOutput, NULL);
	if (ret != 0) {
		printf("Creating AVS output thread failed: %d\n", ret);
		goto __FAILED;
	}

	// Initialize all VI channels and create threads to get VI input
	int thread_params[MAX_VI_CHN] = {0, 1, 2, 3};
	for (int i = 0; i < MAX_VI_CHN; i++) {
		if (chnInfo[i].isActive) {
			vi_fds[i] = vi_chn_init(i, chnInfo[i].deviceName, u32Width, u32Height);
			if (vi_fds[i] < 0) {
				printf("Initializing VI channel %d failed: %d\n", i, vi_fds[i]);
				// Will not exit directly, continue to try other channels
			} else {
				// Create thread to get media data
				ret = pthread_create(&thread_ids[i], NULL, GetMediaBufferAVS,
				                     &thread_params[i]);
				if (ret != 0) {
					printf("Creating thread %d failed: %d\n", i, ret);
				}
			}
		}
	}

	printf("AVS stitching started started - Press Ctrl+C to exit\n");

	// Main thread waits for exit signal
	while (!quit) {
		sleep(1);
	}

	// Wait for all threads to exit
	if (avs_thread_id != 0) {
		pthread_join(avs_thread_id, NULL);
		avs_thread_id = 0;
	}

	for (int i = 0; i < MAX_VI_CHN; i++) {
		if (thread_ids[i] != 0) {
			pthread_join(thread_ids[i], NULL);
			thread_ids[i] = 0;
		}
	}

	// Destroy AVS
	avs_deinit();

	// Close all VI channels
	for (int i = 0; i < MAX_VI_CHN; i++) {
		if (vi_fds[i] >= 0) {
			close(vi_fds[i]);
			s32Ret = RK_MPI_VI_DisableChn(s32PipeId, i);
			printf("Closing VI channel %d: %x\n", i, s32Ret);
			vi_fds[i] = -1;
		}
	}

	// Close VO
	vo_deinit(VoLayer, VoDev, VoChn);

	// Reset saved frame counters for next cycle
	for (int i = 0; i < MAX_VI_CHN; i++) {
		saved_frames[i] = 0;
	}
	avs_saved_frames = 0;
	ret = 0;

__FAILED:
	printf("Program exiting: %d\n", s32Ret);
	RK_MPI_SYS_Exit();
	return ret;
}
