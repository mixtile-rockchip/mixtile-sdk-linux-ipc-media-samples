// Description:
// - 8-channel VI input, each VI channel binds to one VPSS group for 8-grid display
// - VI part reuses the approach from simple_vi_avs_send_vo.c: using virtual pipe, s32PipeId=VI_MAX_PHY_PIPE_NUM
// - Save one frame from each VI channel to /data/simple_multi_ahd_vpss_test/
// - 8 VPSS groups, each group has 5 VPSS channels:
//   VPSS group i ch0: Same resolution as input, encode and save to /data/simple_multi_ahd_vpss_test/venc_grp{i}_main.h264
//   VPSS group i ch1: 704x576 encode and save to /data/simple_multi_ahd_vpss_test/venc_grp{i}_sub.h264
//   VPSS group i ch2: Same resolution as input, save 1 frame to /data/simple_multi_ahd_vpss_test/vpss_grp{i}_ch2_0.yuv
//   VPSS group i ch3: Quad display size(g_disp_w/2 x g_disp_h/2), bind to VO channel i for display
//   VPSS group i ch4: Same resolution as input, manual get frame + send to shared JPEG VENC for jpeg capture
// - OSD overlay: Each channel displays "CH0", "CH1", etc. using RGN (OVERLAY_RGN attached to VENC)
//   OSD appears in VENC streams (main/sub) and JPEG captures (not in VO display)
// - Support extension to 8 VI + 8 VPSS + 8-grid display
// - JPEG capture function: Use single JPEG encoder (channel 16) shared by all channels via manual SendFrame

/*
┌─────────┐    ┌────────────────────────────────────────────────────────────┐
│ video0  │───▶│                   VPSS Group 0                            │─┬─▶ VENC main (1920x1080)
│(VI Ch0) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │ ├─▶ VENC sub (704x576)
└─────────┘    │ │ scale0 │ scale1 │ scale2 │ scale3 │ scale4 │            │ ├─▶ Frame save
               │ │(bypass)│(resize)│(bypass)│(resize)│(bypass)│            │ ├─▶ VO Ch0
               │ │1920x   │704x576 │1920x   │Grid    │1920x   │            │ └──┐
               │ │1080    │        │1080    │Size    │1080    │◀─GetFrame──┼────┘ SendFrame to JPEG
               │ └────────┴────────┴────────┴────────┴────────┘            │
               └────────────────────────────────────────────────────────────┘

┌─────────┐    ┌────────────────────────────────────────────────────────────┐
│ video1  │───▶│                   VPSS Group 1                            │─┬─▶ VENC main (1920x1080)
│(VI Ch1) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │ ├─▶ VENC sub (704x576)
└─────────┘    │ │ scale0 │ scale1 │ scale2 │ scale3 │ scale4 │            │ ├─▶ Frame save
               │ │(bypass)│(resize)│(bypass)│(resize)│(bypass)│            │ ├─▶ VO Ch1
               │ └────────┴────────┴────────┴────────┴────────┘            │ └──┐ SendFrame to JPEG
               └────────────────────────────────────────────────────────────┘    │

┌─────────┐    ┌────────────────────────────────────────────────────────────┐   │
│ video2  │───▶│                   VPSS Group 2                            │ (Same structure)
│(VI Ch2) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │   │
└─────────┘    │ │  ch0   │  ch1   │  ch2   │  ch3   │  ch4   │            │   │
               │ └────────┴────────┴────────┴────────┴────────┘            │   │
               └────────────────────────────────────────────────────────────┘   │

┌─────────┐    ┌────────────────────────────────────────────────────────────┐   │
│ video3  │───▶│                   VPSS Group 3                            │ (Same structure)
│(VI Ch3) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │   │
└─────────┘    │ │  ch0   │  ch1   │  ch2   │  ch3   │  ch4   │            │   │
               │ └────────┴────────┴────────┴────────┴────────┘            │   │
               └────────────────────────────────────────────────────────────┘   │

┌─────────┐    ┌────────────────────────────────────────────────────────────┐   │
│ video11 │───▶│                   VPSS Group 4                            │ (Same structure)
│(VI Ch4) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │   │
└─────────┘    │ │  ch0   │  ch1   │  ch2   │  ch3   │  ch4   │            │   │
               │ └────────┴────────┴────────┴────────┴────────┘            │   │
               └────────────────────────────────────────────────────────────┘   │

┌─────────┐    ┌────────────────────────────────────────────────────────────┐   │
│ video12 │───▶│                   VPSS Group 5                            │ (Same structure)
│(VI Ch5) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │   │
└─────────┘    │ │  ch0   │  ch1   │  ch2   │  ch3   │  ch4   │            │   │
               │ └────────┴────────┴────────┴────────┴────────┘            │   │
               └────────────────────────────────────────────────────────────┘   │

┌─────────┐    ┌────────────────────────────────────────────────────────────┐   │
│ video13 │───▶│                   VPSS Group 6                            │ (Same structure)
│(VI Ch6) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │   │
└─────────┘    │ │  ch0   │  ch1   │  ch2   │  ch3   │  ch4   │            │   │
               │ └────────┴────────┴────────┴────────┴────────┘            │   │
               └────────────────────────────────────────────────────────────┘   │

┌─────────┐    ┌────────────────────────────────────────────────────────────┐   │
│ video14 │───▶│                   VPSS Group 7                            │ (Same structure)
│(VI Ch7) │    │ ┌────────┬────────┬────────┬────────┬────────┐            │   │
└─────────┘    │ │  ch0   │  ch1   │  ch2   │  ch3   │  ch4   │            │   │
               │ └────────┴────────┴────────┴────────┴────────┘            │   │
               └────────────────────────────────────────────────────────────┘   │
                                                                                 │
               All ch4 channels send frames to shared JPEG VENC (round-robin) ◀─┘

Hardware Mapping: 2x TP2815 AHD Decoders
- TP2815#0: video0-3  (I2C3-0x44) -> VI Ch0-3 -> VPSS Group 0-3
- TP2815#1: video11-14 (I2C4-0x44) -> VI Ch4-7 -> VPSS Group 4-7

Multi-Screen Display Grid Layout:
┌─────────┬─────────┬─────────┬─────────┐
│   Ch0   │   Ch1   │   Ch2   │   Ch3   │  ← TP2815#0
│(VPSS0)  │(VPSS1)  │(VPSS2)  │(VPSS3)  │
├─────────┼─────────┼─────────┼─────────┤
│   Ch4   │   Ch5   │   Ch6   │   Ch7   │  ← TP2815#1
│(VPSS4)  │(VPSS5)  │(VPSS6)  │(VPSS7)  │
└─────────┴─────────┴─────────┴─────────┘

====================================================================
VPSS Channel Function Assignment:
VPSS Group 0: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO0) ch4(jpeg)
VPSS Group 1: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO1) ch4(jpeg)
VPSS Group 2: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO2) ch4(jpeg)
VPSS Group 3: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO3) ch4(jpeg)
VPSS Group 4: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO4) ch4(jpeg)
VPSS Group 5: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO5) ch4(jpeg)
VPSS Group 6: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO6) ch4(jpeg)
VPSS Group 7: ch0(main stream) ch1(sub stream) ch2(save) ch3(VO7) ch4(jpeg)

JPEG Capture Workflow (ch4):
  - ch4: Manual GetChnFrame from VPSS ch4 (u32Depth=2)
  - SendFrame to shared JPEG VENC channel 16
  - GetStream from VENC 16 to retrieve JPEG data
  - Save as jpg file (format: jpeg_ch{channel}_{count}.jpg)
  - Only one channel captures at a time (mutex protected, round-robin)

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
├── venc_grp7_sub.h264    # VPSS group 7 sub stream
├── jpeg_ch0_0.jpg        # JPEG capture from channel 0 (count 0)
├── jpeg_ch1_1.jpg        # JPEG capture from channel 1 (count 1)
├── jpeg_ch2_2.jpg        # JPEG capture from channel 2 (count 2)
├── jpeg_ch3_3.jpg        # JPEG capture from channel 3 (count 3)
├── jpeg_ch0_4.jpg        # JPEG capture from channel 0 (count 4, round-robin)
└── ...                   # More JPEG files (enabled with -p 1)

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
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stddef.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_rgn.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"

/* ====================================================================
 * Configuration and Global Variables
 * ====================================================================*/

// Maximum supported channel definitions
#define MAX_VI_CHN 8     // Support up to 8 VI channels
#define MAX_VPSS_GRP 8   // Support up to 8 VPSS groups
#define VENC_CHN_JPEG 16 // JPEG encoder channel (shared by all channels for jpeg capture)

// Global control flags
static volatile RK_BOOL g_quit = RK_FALSE;           // Application exit flag
static RK_BOOL g_enable_switch = RK_FALSE;           // Enable VO dynamic switching
static RK_BOOL g_enable_snapshot = RK_FALSE;         // Enable JPEG capture function
static RK_U32 g_snapshot_interval = 5;               // JPEG capture interval in seconds

// VI input resolution configuration
static RK_U32 g_in_w = 1920;    // VI input width
static RK_U32 g_in_h = 1080;    // VI input height

// VO display resolution configuration (default portrait 1080x1920)
static RK_U32 g_disp_w = 1080;  // VO display width
static RK_U32 g_disp_h = 1920;  // VO display height

// VO display mode enumeration
typedef enum {
    VO_MODE_MULTI = 0,   // Multi-screen grid mode
    VO_MODE_SINGLE = 1   // Single full-screen mode
} VO_DISPLAY_MODE_E;

// Grid layout structure for multi-channel display
typedef struct {
	RK_U32 rows;        // Number of rows in grid
	RK_U32 columns;     // Number of columns in grid
	RK_U32 winWidth;    // Width of each window
	RK_U32 winHeight;   // Height of each window
} GRID_LAYOUT_S;

// VO switching state variables
static VO_DISPLAY_MODE_E g_vo_mode = VO_MODE_MULTI;  // Current VO display mode
static int g_single_chn = 0;                        // Current single-screen channel index
static int g_active_channels = 0;                   // Number of active VI channels
static GRID_LAYOUT_S g_original_layout;             // Saved original grid layout

// VI pipe configuration (use virtual pipe for compatibility)
static int s32PipeId = VI_MAX_PHY_PIPE_NUM;         // VI virtual pipe ID

// JPEG capture related variables
static pthread_mutex_t g_snapshot_mutex = PTHREAD_MUTEX_INITIALIZER; // Protect JPEG VENC access

/* ====================================================================
 * OSD Configuration
 * ====================================================================
 * This section defines parameters for VENC OSD overlay functionality.
 * Each VENC channel has its own OSD region displaying channel identification.
 *
 * Implementation Details:
 * - Type: OVERLAY_RGN (software overlay for VENC channels)
 * - Attachment: Direct to VENC channels (RK_ID_VENC)
 * - Format: ARGB8888 with custom 5x7 dot matrix font
 * - Content: Channel identification text ("CH0", "CH1", etc.)
 * - Position: Configurable, default at top-left (10, 10)
 *
 * Resource Management:
 * - Each VENC channel has its own RGN handle (base + channel ID)
 * - OSD lifecycle is bound to VENC channel lifecycle
 * - Created immediately after VENC channel creation
 * - Destroyed after VENC channel stops but before channel destruction
 */
#define OSD_RGN_BASE_HANDLE 100  /**< Base RGN handle, incremented per VENC channel (100-115 for 16 channels) */
#define OSD_TEXT_WIDTH      80   /**< OSD bitmap width in pixels */
#define OSD_TEXT_HEIGHT     32   /**< OSD bitmap height in pixels */

// VO device configuration (platform dependent)
#ifdef RV1126B
static int g_VoLayer = 0;           // VO layer ID for RV1126B
static int g_VoDev = 0;             // VO device ID for RV1126B
static int g_IntfType = VO_INTF_MIPI; // VO interface type for RV1126B
#elif defined(RV1106_RV1103)
static int g_VoLayer = 0;           // VO layer ID for RV1106/RV1103
static int g_VoDev = 0;             // VO device ID for RV1106/RV1103
static int g_IntfType = VO_INTF_DEFAULT; // VO interface type for RV1106/RV1103
#else
static int g_VoLayer = 5;           // VO layer ID for other platforms
static int g_VoDev = 1;             // VO device ID for other platforms
static int g_IntfType = VO_INTF_MIPI; // VO interface type for other platforms
#endif

/* ====================================================================
 * Data Structures
 * ====================================================================*/

// VI channel information structure
typedef struct {
	int chnId;              // VI channel ID
	char deviceName[64];    // VI device name (e.g., /dev/video0)
	bool isActive;          // Whether this channel is active
	int vpssGrp;            // Corresponding VPSS group number
} VI_CHN_INFO;

// VI channel mapping for 2x TP2815 AHD decoders
static VI_CHN_INFO g_chnInfo[MAX_VI_CHN] = {
    {0, "/dev/video1", false, 0},   // TP2815#0 Channel 0
    {1, "/dev/video2", false, 1},   // TP2815#0 Channel 1
    {2, "/dev/video3", false, 2},   // TP2815#0 Channel 2
    {3, "/dev/video4", false, 3},   // TP2815#0 Channel 3
    {4, "/dev/video11", false, 4},  // TP2815#1 Channel 0
    {5, "/dev/video12", false, 5},  // TP2815#1 Channel 1
    {6, "/dev/video13", false, 6},  // TP2815#1 Channel 2
    {7, "/dev/video14", false, 7},  // TP2815#1 Channel 3
};

// VPSS thread context structure
typedef struct {
	RK_S32 grp;                     // VPSS group ID
	RK_S32 chn;                     // VPSS channel ID
	volatile RK_BOOL *pQuit;        // Pointer to quit flag
} vpss_thread_ctx_t;

// VENC context structure
typedef struct venc_ctx_s {
	int chn;                        // VENC channel ID
	int grp;                        // Corresponding VPSS group
	FILE *fp;                       // Output file pointer
	char out_path[256];             // Output file path
	volatile RK_BOOL *pQuit;        // Pointer to quit flag
} venc_ctx_t;

/* ====================================================================
 * Utility Functions
 * ====================================================================*/

/**
 * Signal handler for graceful shutdown
 * @param sig: Signal number (SIGINT or SIGTERM)
 */
static void sig_handler(int sig) {
	(void)sig;
	if (sig == SIGINT || sig == SIGTERM) {
		printf("Received exit signal %d, shutting down...\n", sig);
		g_quit = RK_TRUE;
	}
}

/**
 * Check if a device file exists
 * @param dev_name: Device file path (e.g., "/dev/video0")
 * @return: RK_TRUE if device exists, RK_FALSE otherwise
 */
static RK_BOOL check_device_exists(const char *dev_name) {
	struct stat st;
	return (stat(dev_name, &st) == 0) ? RK_TRUE : RK_FALSE;
}

/**
 * Ensure output data directory exists
 */
static void ensure_data_dir(void) {
	system("mkdir -p /data/simple_multi_ahd_vpss_test >/dev/null 2>&1");
}

/**
 * Save one YUV frame to file
 * @param path: Output file path
 * @param f: Video frame info structure
 * @return: 0 on success, -1 on failure
 */
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

/* ====================================================================
 * OSD Functions - Simple 5x7 dot matrix font implementation
 * ====================================================================*/

// 5x7 dot matrix font data for characters: C, H, 0-9
// Each character is 5 bytes, representing 5 columns of 7 rows (bits 0-6 used)
static const unsigned char g_font_5x7[][5] = {
	// 'C'
	{0x3E, 0x41, 0x41, 0x41, 0x22},
	// 'H'
	{0x7F, 0x08, 0x08, 0x08, 0x7F},
	// '0'
	{0x3E, 0x51, 0x49, 0x45, 0x3E},
	// '1'
	{0x00, 0x42, 0x7F, 0x40, 0x00},
	// '2'
	{0x62, 0x51, 0x49, 0x49, 0x46},
	// '3'
	{0x22, 0x41, 0x49, 0x49, 0x36},
	// '4'
	{0x18, 0x14, 0x12, 0x7F, 0x10},
	// '5'
	{0x27, 0x45, 0x45, 0x45, 0x39},
	// '6'
	{0x3C, 0x4A, 0x49, 0x49, 0x30},
	// '7'
	{0x01, 0x71, 0x09, 0x05, 0x03},
	// '8'
	{0x36, 0x49, 0x49, 0x49, 0x36},
	// '9'
	{0x06, 0x49, 0x49, 0x29, 0x1E}
};

// Font index mapping
#define FONT_C  0
#define FONT_H  1
#define FONT_0  2
#define FONT_1  3
#define FONT_2  4
#define FONT_3  5
#define FONT_4  6
#define FONT_5  7
#define FONT_6  8
#define FONT_7  9
#define FONT_8  10
#define FONT_9  11

/**
 * Draw a single character on ARGB8888 bitmap
 * @param bitmap: Pointer to ARGB8888 bitmap buffer
 * @param width: Bitmap width
 * @param height: Bitmap height
 * @param x: X position to draw character
 * @param y: Y position to draw character
 * @param font_idx: Font index from g_font_5x7
 * @param color: ARGB8888 color value (0xAARRGGBB)
 */
static void draw_char_5x7(RK_U32 *bitmap, int width, int height, int x, int y, int font_idx, RK_U32 color) {
	if (font_idx < 0 || font_idx >= (int)(sizeof(g_font_5x7) / sizeof(g_font_5x7[0])))
		return;

	const unsigned char *font_data = g_font_5x7[font_idx];

	// Draw 5x7 character
	for (int col = 0; col < 5; col++) {
		for (int row = 0; row < 7; row++) {
			if (font_data[col] & (1 << row)) {
				int px = x + col * 2;  // Scale 2x for better visibility
				int py = y + row * 2;

				// Draw 2x2 pixel block for each dot
				for (int dy = 0; dy < 2; dy++) {
					for (int dx = 0; dx < 2; dx++) {
						int px_final = px + dx;
						int py_final = py + dy;

						if (px_final >= 0 && px_final < width && py_final >= 0 && py_final < height) {
							bitmap[py_final * width + px_final] = color;
						}
					}
				}
			}
		}
	}
}

/**
 * Create OSD bitmap with channel text (e.g., "CH0", "CH1", etc.)
 * @param channel: Channel number (0-7)
 * @param bitmap: Output ARGB8888 bitmap buffer (actually ABGR format)
 * @param width: Bitmap width
 * @param height: Bitmap height
 * @return: 0 on success, -1 on failure
 *
 * Note: RK platform uses ABGR byte order (Alpha-Blue-Green-Red)
 * Color format: 0xAABBGGRR
 */
static int create_channel_osd_bitmap(int channel, RK_U32 *bitmap, int width, int height) {
	if (!bitmap || channel < 0 || channel > 7)
		return -1;

	// Clear bitmap with transparent background
	// Format: 0xAABBGGRR (Alpha-Blue-Green-Red)
	RK_U32 bg_transparent = 0x00000000;  // Fully transparent
	for (int i = 0; i < width * height; i++) {
		bitmap[i] = bg_transparent;
	}

	// Draw semi-transparent black background box for text area
	// Background: semi-transparent black (R=00, G=00, B=00, A=C0)
	// ABGR format: 0xC0000000
	RK_U32 bg_box = 0xC0000000;
	int text_area_w = 50;  // Width of text area (3 chars * 12 + margins)
	int text_area_h = 22;  // Height of text area

	for (int py = 2; py < text_area_h; py++) {
		for (int px = 2; px < text_area_w; px++) {
			if (py < height && px < width) {
				bitmap[py * width + px] = bg_box;
			}
		}
	}

	// Text color: opaque yellow (R=FF, G=FF, B=00, A=FF)
	// ABGR format: 0xFF00FFFF
	RK_U32 text_color = 0xFF00FFFF;

	// Draw "CH" at position (4, 4)
	int x = 4;
	int y = 4;

	// Draw 'C'
	draw_char_5x7(bitmap, width, height, x, y, FONT_C, text_color);
	x += 12;  // 5*2 + 2 spacing

	// Draw 'H'
	draw_char_5x7(bitmap, width, height, x, y, FONT_H, text_color);
	x += 12;

	// Draw channel number
	int font_idx = FONT_0 + channel;
	draw_char_5x7(bitmap, width, height, x, y, font_idx, text_color);

	printf("OSD: Created text 'CH%d' for channel %d (%dx%d, yellow text on semi-transparent black)\n",
	       channel, channel, width, height);

	return 0;
}

/* ====================================================================
 * VENC OSD Management Functions
 * ====================================================================
 * These functions manage OSD overlay for VENC channels.
 * Each VENC channel gets its own OSD region displaying channel identification.
 *
 * Key Features:
 * - Uses OVERLAY_RGN type (software overlay for VENC)
 * - Direct attachment to VENC channels (RK_ID_VENC)
 * - ARGB8888 pixel format for text rendering
 * - Lifecycle bound to VENC channel (create after VENC, destroy before VENC)
 *
 * Usage:
 * 1. Call venc_create_osd() immediately after RK_MPI_VENC_CreateChn()
 * 2. Call venc_destroy_osd() after RK_MPI_VENC_StopRecvFrame() but before RK_MPI_VENC_DestroyChn()
 */

/**
 * Create and attach OSD to a VENC channel
 *
 * This function creates an OSD region with channel identification text (e.g., "CH0", "CH1")
 * and attaches it directly to the specified VENC channel. The OSD will appear in all
 * encoded streams from this channel.
 *
 * @param venc_chn VENC channel ID (0-15)
 * @param display_channel Channel number to display in OSD text (0-7)
 * @return 0 on success, -1 on failure
 *
 * @note This function should be called immediately after VENC channel creation
 * @note Uses OVERLAY_RGN type which is appropriate for VENC software overlay
 */
static int venc_create_osd(int venc_chn, int display_channel) {
	RK_S32 ret;
	RGN_HANDLE rgnHandle = OSD_RGN_BASE_HANDLE + venc_chn;  // Each VENC channel has unique RGN handle
	RGN_ATTR_S rgnAttr;
	RGN_CHN_ATTR_S chnAttr;
	BITMAP_S bitmap;
	MPP_CHN_S mppChn;

	printf("Creating OSD for VENC channel %d (display: CH%d)\n", venc_chn, display_channel);

	// Step 1: Create RGN region with OVERLAY_RGN type
	// OVERLAY_RGN is used for VENC software overlay (not hardware overlay)
	memset(&rgnAttr, 0, sizeof(rgnAttr));
	rgnAttr.enType = OVERLAY_RGN;
	rgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	rgnAttr.unAttr.stOverlay.stSize.u32Width = OSD_TEXT_WIDTH;
	rgnAttr.unAttr.stOverlay.stSize.u32Height = OSD_TEXT_HEIGHT;
	rgnAttr.unAttr.stOverlay.u32ClutNum = 0;

	ret = RK_MPI_RGN_Create(rgnHandle, &rgnAttr);
	if (ret != RK_SUCCESS) {
		printf("Failed to create RGN handle %d for VENC %d: 0x%x\n", rgnHandle, venc_chn, ret);
		return -1;
	}

	// Step 2: Create ARGB8888 bitmap with channel identification text
	RK_U32 *bitmapData = (RK_U32 *)malloc(OSD_TEXT_WIDTH * OSD_TEXT_HEIGHT * 4);
	if (!bitmapData) {
		printf("Failed to allocate bitmap memory\n");
		RK_MPI_RGN_Destroy(rgnHandle);
		return -1;
	}

	create_channel_osd_bitmap(display_channel, bitmapData, OSD_TEXT_WIDTH, OSD_TEXT_HEIGHT);

	// Step 3: Set bitmap data to RGN region
	memset(&bitmap, 0, sizeof(bitmap));
	bitmap.enPixelFormat = RK_FMT_ARGB8888;
	bitmap.u32Width = OSD_TEXT_WIDTH;
	bitmap.u32Height = OSD_TEXT_HEIGHT;
	bitmap.pData = bitmapData;

	ret = RK_MPI_RGN_SetBitMap(rgnHandle, &bitmap);
	if (ret != RK_SUCCESS) {
		printf("Failed to set bitmap for VENC %d: 0x%x\n", venc_chn, ret);
		free(bitmapData);
		RK_MPI_RGN_Destroy(rgnHandle);
		return -1;
	}

	free(bitmapData);  // Bitmap data copied to RGN, safe to free

	// Step 4: Attach RGN to VENC channel
	// Configure channel attributes for overlay display
	memset(&mppChn, 0, sizeof(mppChn));
	mppChn.enModId = RK_ID_VENC;
	mppChn.s32DevId = 0;
	mppChn.s32ChnId = venc_chn;

	memset(&chnAttr, 0, sizeof(chnAttr));
	chnAttr.bShow = RK_TRUE;
	chnAttr.enType = OVERLAY_RGN;
	chnAttr.unChnAttr.stOverlayChn.stPoint.s32X = 10;  // OSD position: top-left corner
	chnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = 10;
	chnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;   // Semi-transparent background
	chnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;   // Semi-transparent foreground
	chnAttr.unChnAttr.stOverlayChn.u32ColorLUT[0] = 0x00;
	chnAttr.unChnAttr.stOverlayChn.u32ColorLUT[1] = 0xFFFFFF;
	chnAttr.unChnAttr.stOverlayChn.u32Layer = 0;
	chnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = RK_FALSE;
	chnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp = RK_FALSE;

	ret = RK_MPI_RGN_AttachToChn(rgnHandle, &mppChn, &chnAttr);
	if (ret != RK_SUCCESS) {
		printf("Failed to attach RGN to VENC %d: 0x%x\n", venc_chn, ret);
		RK_MPI_RGN_Destroy(rgnHandle);
		return -1;
	}

	printf("OSD attached to VENC channel %d (CH%d)\n", venc_chn, display_channel);
	return 0;
}

/**
 * Destroy OSD for a VENC channel
 *
 * This function detaches the OSD region from the VENC channel and destroys the RGN resource.
 * It should be called after the VENC channel has stopped receiving frames but before
 * the channel is destroyed.
 *
 * @param venc_chn VENC channel ID (0-15)
 * @return 0 on success (always returns 0 to allow cleanup to continue)
 *
 * @note Call this after RK_MPI_VENC_StopRecvFrame() but before RK_MPI_VENC_DestroyChn()
 * @note For OVERLAY_RGN, detach and destroy operations are synchronous and immediate
 */
static int venc_destroy_osd(int venc_chn) {
	RGN_HANDLE rgnHandle = OSD_RGN_BASE_HANDLE + venc_chn;
	MPP_CHN_S mppChn;
	RK_S32 ret;

	printf("Destroying OSD for VENC channel %d (handle %d)...\n", venc_chn, rgnHandle);

	// Step 1: Detach RGN from VENC channel
	memset(&mppChn, 0, sizeof(mppChn));
	mppChn.enModId = RK_ID_VENC;
	mppChn.s32DevId = 0;
	mppChn.s32ChnId = venc_chn;

	ret = RK_MPI_RGN_DetachFromChn(rgnHandle, &mppChn);
	if (ret == RK_SUCCESS) {
		printf("Detached RGN from VENC channel %d\n", venc_chn);
	} else {
		printf("Failed to detach RGN from VENC %d: 0x%x\n", venc_chn, ret);
	}

	// Step 2: Destroy RGN region
	ret = RK_MPI_RGN_Destroy(rgnHandle);
	if (ret != RK_SUCCESS) {
		printf("Failed to destroy RGN handle %d: 0x%x\n", rgnHandle, ret);
	} else {
		printf("OSD destroyed for VENC %d\n", venc_chn);
	}

	return 0;  // Always return success to allow cleanup to continue
}

/* ====================================================================
 * Grid Layout Functions
 * ====================================================================*/

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

/* ====================================================================
 * VO Dynamic Switching Functions
 * ====================================================================*/

/**
 * VO dynamic switching function - High-performance switching without destruction
 *
 * Core optimization: Only modify VO channel attributes, no VO/VPSS destruction
 * APIs used: PauseComposer + SetChnAttr + HideChn/ShowChn + ResumeComposer
 *
 * @param VoLayer: VO layer ID
 * @param newMode: Target display mode (multi-screen/single-screen)
 * @param targetChn: Target channel for single-screen mode
 * @param numChannels: Total number of channels
 * @return: 0 on success, non-zero on failure
 */
static int vo_switch_mode(int VoLayer, VO_DISPLAY_MODE_E newMode, int targetChn, int numChannels) {
	RK_S32 ret = RK_SUCCESS;
	VO_CHN_ATTR_S stChnAttr;

	printf("VO switching from %s to %s mode, target channel: %d\n",
	       (g_vo_mode == VO_MODE_MULTI) ? "MULTI" : "SINGLE",
	       (newMode == VO_MODE_MULTI) ? "MULTI" : "SINGLE", targetChn);
#ifdef RV1126B
	// Step 1: Pause composer to avoid flickering during switch
	ret = RK_MPI_VO_PauseComposer(VoLayer);
	if (ret != RK_SUCCESS) {
		printf("Failed to pause composer: 0x%x\n", ret);
		return ret;
	}
#endif
	if (newMode == VO_MODE_SINGLE) {
		// Step 2a: Switch to single-screen mode
		memset(&stChnAttr, 0, sizeof(VO_CHN_ATTR_S));
		ret = RK_MPI_VO_GetChnAttr(VoLayer, targetChn, &stChnAttr);
		if (ret != RK_SUCCESS) {
			printf("Failed to get channel %d attr: 0x%x\n", targetChn, ret);
#ifdef RV1126B
			RK_MPI_VO_ResumeComposer(VoLayer);
#endif
			return ret;
		}

		// Configure target channel for full-screen display
		stChnAttr.u32Priority = 10;             // Highest priority to ensure foreground
		stChnAttr.stRect.s32X = 0;              // Full-screen position
		stChnAttr.stRect.s32Y = 0;
		stChnAttr.stRect.u32Width = g_disp_w;   // Full-screen size
		stChnAttr.stRect.u32Height = g_disp_h;
		stChnAttr.u32FgAlpha = 255;             // Fully opaque
		stChnAttr.u32BgAlpha = 0;
		// Critical fix: Explicitly set correct rotation angle
		stChnAttr.enRotation = (g_disp_w == 1080 && g_disp_h == 1920) ? ROTATION_270 : ROTATION_0;
		printf("Single mode: Channel %d rotation set to %d (display %dx%d)\n",
		       targetChn, stChnAttr.enRotation, g_disp_w, g_disp_h);

		ret = RK_MPI_VO_SetChnAttr(VoLayer, targetChn, &stChnAttr);
		if (ret != RK_SUCCESS) {
			printf("Failed to set channel %d attr: 0x%x\n", targetChn, ret);
#ifdef RV1126B
			RK_MPI_VO_ResumeComposer(VoLayer);
#endif
			return ret;
		}

		// Hide all other channels, only show target channel
		for (int i = 0; i < numChannels; i++) {
			if (i != targetChn) {
				ret = RK_MPI_VO_HideChn(VoLayer, i);
				if (ret != RK_SUCCESS) {
					printf("Failed to hide channel %d: 0x%x\n", i, ret);
				}
			}
		}

		printf("Switched to single screen mode: channel %d full screen\n", targetChn);

	} else {
		// Step 2b: Switch to multi-screen mode - restore grid layout
		for (int i = 0; i < numChannels; i++) {
			RK_U32 winX, winY;
			calculate_window_position(i, &g_original_layout, &winX, &winY);

			memset(&stChnAttr, 0, sizeof(VO_CHN_ATTR_S));
			ret = RK_MPI_VO_GetChnAttr(VoLayer, i, &stChnAttr);
			if (ret != RK_SUCCESS) {
				printf("Failed to get channel %d attr: 0x%x\n", i, ret);
				continue;
			}

			// Restore each channel to grid layout
			stChnAttr.u32Priority = i;                          // Restore original priority
			stChnAttr.stRect.s32X = winX;                       // Grid position
			stChnAttr.stRect.s32Y = winY;
			stChnAttr.stRect.u32Width = g_original_layout.winWidth;   // Grid size
			stChnAttr.stRect.u32Height = g_original_layout.winHeight;
			stChnAttr.u32FgAlpha = 255;                         // Fully opaque
			stChnAttr.u32BgAlpha = 0;
			// Critical fix: Explicitly set correct rotation angle
			stChnAttr.enRotation = (g_disp_w == 1080 && g_disp_h == 1920) ? ROTATION_270 : ROTATION_0;
			printf("Multi mode: Channel %d rotation set to %d (display %dx%d)\n",
			       i, stChnAttr.enRotation, g_disp_w, g_disp_h);

			ret = RK_MPI_VO_SetChnAttr(VoLayer, i, &stChnAttr);
			if (ret != RK_SUCCESS) {
				printf("Failed to set channel %d attr: 0x%x\n", i, ret);
				continue;
			}

			// Show all channels
			ret = RK_MPI_VO_ShowChn(VoLayer, i);
			if (ret != RK_SUCCESS) {
				printf("Failed to show channel %d: 0x%x\n", i, ret);
			}
		}

		printf("Switched to multi-screen mode: %dx%d grid\n",
		       g_original_layout.columns, g_original_layout.rows);
	}
#ifdef RV1126B
	// Step 3: Resume composer to complete the switch
	ret = RK_MPI_VO_ResumeComposer(VoLayer);
	if (ret != RK_SUCCESS) {
		printf("Failed to resume composer: 0x%x\n", ret);
		return ret;
	}
#endif
	// Update global state
	g_vo_mode = newMode;
	return RK_SUCCESS;
}

/* ====================================================================
 * VI/VO/VPSS Initialization Functions
 * ====================================================================*/

/**
 * Initialize VI channel
 * @param channelId: VI channel ID
 * @param video_dev: Video device path (e.g., "/dev/video0")
 * @param width: Input video width
 * @param height: Input video height
 * @return: 0 on success, non-zero on failure
 */
static int vi_chn_init(int channelId, const char *video_dev, int width, int height) {
	int ret;
	VI_CHN_ATTR_S stChnAttr;
	memset(&stChnAttr, 0, sizeof(stChnAttr));
	strncpy(stChnAttr.stIspOpt.aEntityName, video_dev,
	        sizeof(stChnAttr.stIspOpt.aEntityName) - 1);
	stChnAttr.stIspOpt.u32BufCount = 8;
	stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
	stChnAttr.stIspOpt.bNoUseLibV4L2 = RK_FALSE;
	stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
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

/**
 * Initialize VO (Video Output) with multi-channel grid display support
 * Supports 1-8 channels with automatic grid layout calculation
 *
 * @param VoLayer: VO layer ID
 * @param VoDev: VO device ID
 * @param Width: Display width
 * @param Height: Display height
 * @param IntfType: Interface type (MIPI/HDMI/etc.)
 * @param numChannels: Number of channels (1-8)
 * @return: 0 on success, non-zero on failure
 */
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

	// Save original layout for dynamic switching
	g_original_layout = layout;

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
		stChnAttr.u32Priority = i;  // Set priority same as channel ID for easy switching
		stChnAttr.enRotation = (Width == 1080 && Height == 1920) ? ROTATION_270 : ROTATION_0;

		printf("VO Channel %d: position(%d,%d), size(%dx%d), rotation=%d\n",
		       i, winX, winY, layout.winWidth, layout.winHeight, stChnAttr.enRotation);

		RK_MPI_VO_SetChnAttr(VoLayer, i, &stChnAttr);
		RK_MPI_VO_EnableChn(VoLayer, i);
	}

	return 0;
}

/**
 * Initialize VPSS group with 5 channels
 * Channel allocation:
 * - ch0: Main stream encoding (original resolution)
 * - ch1: Sub stream encoding (704x576)
 * - ch2: Frame saving (original resolution)
 * - ch3: VO display (grid layout size)
 * - ch4: Snapshot (original resolution, dynamically bind to JPEG VENC)
 *
 * @param grp: VPSS group ID
 * @param inW: Input width
 * @param inH: Input height
 * @param voW: VO display width per channel
 * @param voH: VO display height per channel
 * @return: 0 on success, non-zero on failure
 */
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

	// ch4: For snapshot, original resolution, u32Depth>0 for manual frame retrieval
	c.u32Width = inW;
	c.u32Height = inH;
	c.u32Depth = 2;  // Need depth > 0 to manually get frames
	RK_MPI_VPSS_SetChnAttr(grp, 4, &c);
	RK_MPI_VPSS_EnableChn(grp, 4);

	return RK_MPI_VPSS_StartGrp(grp);
}

/* ====================================================================
 * Thread Functions
 * ====================================================================*/

/**
 * VPSS channel 2 data acquisition thread
 * This thread gets frames from VPSS channel 2 and saves one frame per channel
 *
 * @param arg: Pointer to vpss_thread_ctx_t structure
 * @return: NULL
 */
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

/**
 * Create and start VENC encoder
 * @param chn: VENC channel ID
 * @param grp: Corresponding VPSS group
 * @param w: Video width
 * @param h: Video height
 * @param type: Codec type (H264/H265)
 * @param ctx: VENC context structure
 * @param quitFlag: Pointer to quit flag
 * @return: 0 on success, -1 on failure
 */
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

/**
 * VENC stream retrieval thread
 * This thread continuously gets encoded streams from VENC and writes to file
 *
 * @param arg: Pointer to venc_ctx_t structure
 * @return: NULL
 */
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

/**
 * Create and initialize JPEG encoder for jpeg capture
 * @param chn: VENC channel ID
 * @param w: Image width
 * @param h: Image height
 * @return: 0 on success, non-zero on failure
 */
static int jpeg_venc_create(int chn, int w, int h) {
	VENC_CHN_ATTR_S attr;
	memset(&attr, 0, sizeof(attr));

	attr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
	attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	attr.stVencAttr.u32MaxPicWidth = w;
	attr.stVencAttr.u32MaxPicHeight = h;
	attr.stVencAttr.u32PicWidth = w;
	attr.stVencAttr.u32PicHeight = h;
	attr.stVencAttr.u32VirWidth = w;
	attr.stVencAttr.u32VirHeight = h;
	attr.stVencAttr.u32StreamBufCnt = 2;
	attr.stVencAttr.u32BufSize = w * h * 3 / 2;

	if (RK_MPI_VENC_CreateChn(chn, &attr) != RK_SUCCESS) {
		printf("Failed to create JPEG VENC channel %d\n", chn);
		return -1;
	}

	// Note: Don't start receiving frames here, will be controlled per capture
	printf("JPEG VENC channel %d created (resolution: %dx%d)\n", chn, w, h);
	return 0;
}

/**
 * JPEG capture function - capture one JPEG image from specified channel
 * Uses manual frame retrieval + send frame mode: get frame from VPSS, send to JPEG VENC
 *
 * @param vpssGrp: VPSS group ID
 * @param vpssGrpChn: VPSS channel ID (typically ch4)
 * @param vencChn: JPEG VENC channel ID
 * @param savePath: Output JPEG file path
 * @return: 0 on success, non-zero on failure
 */
static int take_snapshot(int vpssGrp, int vpssGrpChn, int vencChn, const char *savePath) {
	RK_S32 ret;
	VIDEO_FRAME_INFO_S stFrame;
	VENC_STREAM_S stream;
	VENC_RECV_PIC_PARAM_S recvParam;
	FILE *fp = NULL;

	// Lock mutex to ensure exclusive access to JPEG VENC
	pthread_mutex_lock(&g_snapshot_mutex);

	// Get one frame from VPSS channel
	memset(&stFrame, 0, sizeof(stFrame));
	ret = RK_MPI_VPSS_GetChnFrame(vpssGrp, vpssGrpChn, &stFrame, 1000);
	if (ret != RK_SUCCESS) {
		printf("Failed to get frame from VPSS(%d,%d): 0x%x\n", vpssGrp, vpssGrpChn, ret);
		pthread_mutex_unlock(&g_snapshot_mutex);
		return -1;
	}

	// Start receiving only 1 frame for this snapshot
	memset(&recvParam, 0, sizeof(recvParam));
	recvParam.s32RecvPicNum = 1;  // Only receive 1 frame

	ret = RK_MPI_VENC_StartRecvFrame(vencChn, &recvParam);
	if (ret != RK_SUCCESS) {
		printf("Failed to start recv frame for VENC(%d): 0x%x\n", vencChn, ret);
		RK_MPI_VPSS_ReleaseChnFrame(vpssGrp, vpssGrpChn, &stFrame);
		pthread_mutex_unlock(&g_snapshot_mutex);
		return -1;
	}

	// Send frame to JPEG VENC
	ret = RK_MPI_VENC_SendFrame(vencChn, &stFrame, 1000);
	if (ret != RK_SUCCESS) {
		printf("Failed to send frame to VENC(%d): 0x%x\n", vencChn, ret);
		RK_MPI_VENC_StopRecvFrame(vencChn);
		RK_MPI_VPSS_ReleaseChnFrame(vpssGrp, vpssGrpChn, &stFrame);
		pthread_mutex_unlock(&g_snapshot_mutex);
		return -1;
	}

	// Release VPSS frame immediately after sending
	RK_MPI_VPSS_ReleaseChnFrame(vpssGrp, vpssGrpChn, &stFrame);

	// Get JPEG stream (wait up to 2 seconds)
	memset(&stream, 0, sizeof(stream));
	stream.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
	if (!stream.pstPack) {
		printf("Failed to allocate memory for VENC stream\n");
		RK_MPI_VENC_StopRecvFrame(vencChn);
		pthread_mutex_unlock(&g_snapshot_mutex);
		return -1;
	}

	ret = RK_MPI_VENC_GetStream(vencChn, &stream, 2000);
	if (ret == RK_SUCCESS) {
		// Save JPEG to file
		fp = fopen(savePath, "wb");
		if (fp) {
			void *data = RK_MPI_MB_Handle2VirAddr(stream.pstPack->pMbBlk);
			RK_U32 len = stream.pstPack->u32Len;
			if (data && len > 0) {
				fwrite(data, 1, len, fp);
				printf("JPEG saved: %s (size: %u bytes)\n", savePath, len);
			}
			fclose(fp);
		} else {
			printf("Failed to open file: %s\n", savePath);
		}
		RK_MPI_VENC_ReleaseStream(vencChn, &stream);
	} else {
		printf("Failed to get JPEG stream from channel %d: 0x%x\n", vencChn, ret);
	}

	free(stream.pstPack);

	// Stop receiving frames
	RK_MPI_VENC_StopRecvFrame(vencChn);

	pthread_mutex_unlock(&g_snapshot_mutex);

	return (ret == RK_SUCCESS) ? 0 : -1;
}

/**
 * JPEG capture control thread
 * Periodically triggers JPEG capture for each channel in round-robin fashion
 * @param arg: Pointer to number of active channels
 * @return: NULL
 */
static void *snapshot_thread(void *arg) {
	int numChannels = *(int *)arg;
	int currentChn = 0;
	char filePath[256];
	int captureCount = 0;

	printf("JPEG capture thread started, interval: %d seconds, channels: %d\n",
	       g_snapshot_interval, numChannels);

	while (!g_quit) {
		// Wait for capture interval
		for (int i = 0; i < g_snapshot_interval * 10 && !g_quit; i++) {
			usleep(100000); // 100ms
		}

		if (g_quit) break;

		// Find next active channel
		int attempts = 0;
		while (attempts < numChannels) {
			if (g_chnInfo[currentChn].isActive) {
				// Take JPEG capture from current channel
				snprintf(filePath, sizeof(filePath),
				         "/data/simple_multi_ahd_vpss_test/jpeg_ch%d_%d.jpg",
				         currentChn, captureCount);

				printf("Taking JPEG capture: channel %d -> %s\n", currentChn, filePath);
				take_snapshot(currentChn, 4, VENC_CHN_JPEG, filePath);

				captureCount++;
				currentChn = (currentChn + 1) % MAX_VI_CHN;
				break;
			}
			currentChn = (currentChn + 1) % MAX_VI_CHN;
			attempts++;
		}
	}

	printf("JPEG capture thread exiting, total captures: %d\n", captureCount);
	return NULL;
}

/**
 * VO mode switching control thread
 * This thread automatically switches between multi-screen and single-screen modes
 *
 * @param arg: Pointer to number of channels
 * @return: NULL
 */
static void *vo_switch_thread(void *arg) {
	int *numChannels = (int *)arg;
	int switch_interval = 5; // Switch interval in seconds

	printf("VO switch thread started, will switch every %d seconds\n", switch_interval);
	printf("Current mode: %s\n", (g_vo_mode == VO_MODE_MULTI) ? "MULTI" : "SINGLE");

	while (!g_quit) {
		// Wait for switch interval
		for (int i = 0; i < switch_interval * 10 && !g_quit; i++) {
			usleep(100000); // 100ms
		}

		if (g_quit) break;

		// Execute automatic switching
		if (g_vo_mode == VO_MODE_MULTI) {
			// Switch from multi-screen to single-screen, cycle through channels
			g_single_chn = (g_single_chn + 1) % (*numChannels);
			vo_switch_mode(g_VoLayer, VO_MODE_SINGLE, g_single_chn, *numChannels);
		} else {
			// Switch from single-screen back to multi-screen
			vo_switch_mode(g_VoLayer, VO_MODE_MULTI, 0, *numChannels);
		}
	}

	printf("VO switch thread exiting\n");
	return NULL;
}

/**
 * Print program usage information
 * @param n: Program name
 */
static void print_usage(const char *n) {
	printf("usage: %s [-w in_w] [-h in_h] [-W vo_w] [-H vo_h] [-n num_channels] [-s switch_mode] [-p snapshot_mode] [-i snapshot_interval]\n", n);
	printf("  -w: VI input width (default: 1920)\n");
	printf("  -h: VI input height (default: 1080)\n");
	printf("  -W: VO display width (default: 1080)\n");
	printf("  -H: VO display height (default: 1920)\n");
	printf("  -n: Number of VI/VPSS channels (1-8, default: 4)\n");
	printf("  -s: Enable VO dynamic switch (0=disabled, 1=enabled, default: 0)\n");
	printf("  -p: Enable JPEG capture function (0=disabled, 1=enabled, default: 0)\n");
	printf("  -i: JPEG capture interval in seconds (default: 5)\n");
	printf("\nSupported grid layouts:\n");
	printf("  1 channel  -> 1x1 grid (full screen)\n");
	printf("  2 channels -> 1x2 grid (side by side)\n");
	printf("  3-4 channels -> 2x2 grid (quad view)\n");
	printf("  5-6 channels -> 2x3 grid\n");
	printf("  7-8 channels -> 2x4 grid\n");
	printf("\nExamples:\n");
	printf("  %s -n 1                           # Single channel static display\n", n);
	printf("  %s -n 2 -s 1                      # Dual channel with dynamic switch\n", n);
	printf("  %s -n 4 -s 1                      # Quad view with automatic switch test\n", n);
	printf("  %s -n 4 -p 1 -i 3                 # Quad view with JPEG capture every 3 seconds\n", n);
	printf("  %s -w 1920 -h 1080 -W 1080 -H 1920 -n 4 -s 1  # Full parameter example\n", n);
	printf("  %s -n 8 -p 1                      # Eight channels with JPEG capture\n", n);
}

/**
 * Main function - Multi-channel AHD VPSS test program
 *
 * Features:
 * - Support 1-8 VI channels with 2x TP2815 AHD decoders
 * - Each VI channel binds to one VPSS group for processing
 * - VPSS channels: ch0(main encode), ch1(sub encode), ch2(save), ch3(VO display)
 * - Dynamic VO switching between multi-screen grid and single-screen modes
 * - Automatic grid layout calculation based on channel count
 * - Support portrait mode with 270° rotation
 *
 * @param argc: Command line argument count
 * @param argv: Command line argument array
 * @return: 0 on success, -1 on failure
 */
int main(int argc, char *argv[]) {
	int c;
	int num_channels = 4;  // Default 4 channels

	while ((c = getopt(argc, argv, "w:h:W:H:n:s:p:i:?")) != -1) {
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
		case 's':
			g_enable_switch = (atoi(optarg) == 1) ? RK_TRUE : RK_FALSE;
			printf("VO switch mode %s\n", g_enable_switch ? "ENABLED" : "DISABLED");
			break;
		case 'p':
			g_enable_snapshot = (atoi(optarg) == 1) ? RK_TRUE : RK_FALSE;
			printf("JPEG capture mode %s\n", g_enable_snapshot ? "ENABLED" : "DISABLED");
			break;
		case 'i':
			g_snapshot_interval = atoi(optarg);
			if (g_snapshot_interval < 1) {
				printf("Invalid JPEG capture interval, using default: 5 seconds\n");
				g_snapshot_interval = 5;
			}
			printf("JPEG capture interval: %d seconds\n", g_snapshot_interval);
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

	g_active_channels = active;  // Save active channel count

	// Initialize VI channels
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
	venc_ctx_t vencCtx[MAX_VPSS_GRP * 2]; // 2 encoders per group (main/sub)
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

		// Create OSD for main stream (right after VENC created)
		venc_create_osd(venc_count, i);  // VENC channel, display "CHi"

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

		// Create OSD for sub stream (right after VENC created)
		venc_create_osd(venc_count, i);  // VENC channel, display "CHi"

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

	printf("System initialized, displaying %dx%d grid (%d channels)\n",
	       voLayout.columns, voLayout.rows, active);

	// Create JPEG encoder for jpeg capture (if enabled)
	pthread_t snapshotThread;
	if (g_enable_snapshot) {
		if (jpeg_venc_create(VENC_CHN_JPEG, g_in_w, g_in_h) == 0) {
			pthread_create(&snapshotThread, NULL, snapshot_thread, &active);
			printf("JPEG Capture Mode ENABLED - capturing every %d seconds\n", g_snapshot_interval);
			printf("JPEG files saved to: /data/simple_multi_ahd_vpss_test/jpeg_chX_N.jpg\n");
		} else {
			printf("Failed to create JPEG encoder, jpeg capture disabled\n");
			g_enable_snapshot = RK_FALSE;
		}
	}

	// Start VO switching control thread (only when enabled)
	pthread_t voSwitchThread;

	if (g_enable_switch) {
		pthread_create(&voSwitchThread, NULL, vo_switch_thread, &active);
		printf("VO Dynamic Switch Mode - automatic switching every 5 seconds\n");
	} else {
		printf("VO Static Display Mode (use -s 1 to enable switching)\n");
	}
	printf("Press Ctrl+C to quit\n");

	while (!g_quit)
		sleep(1);

	printf("Shutting down...\n");

	// Step 1: Wait for control threads to exit first
	if (g_enable_switch) {
		pthread_join(voSwitchThread, NULL);
		printf("VO switch thread stopped\n");
	}

	if (g_enable_snapshot) {
		pthread_join(snapshotThread, NULL);
		printf("Snapshot thread stopped\n");
	}

	// Step 2: Wait for VPSS ch2 threads to exit
	for (int i = 0; i < num_channels; ++i) {
		if (g_chnInfo[i].isActive) {
			pthread_join(vpssThreads[i], NULL);
		}
	}
	printf("VPSS threads stopped\n");

	// Step 3: Stop VENC channels before destroying OSD
	// CRITICAL: Must stop VENC first, otherwise VENC may still be using OSD buffers
	printf("Stopping VENC (before destroying OSD)...\n");
	for (int i = 0; i < venc_count; ++i) {
		RK_MPI_VENC_StopRecvFrame(i);  // Stop receiving frames
	}
	// Wait for VENC threads to finish
	for (int i = 0; i < venc_count; ++i) {
		pthread_join(vencThreads[i], NULL);
		if (vencCtx[i].fp) {
			fclose(vencCtx[i].fp);
		}
	}
	printf("VENC receive stopped, threads joined\n");

	// Step 4: Destroy OSD for all VENC channels (after VENC stopped)
	// Each VENC channel has its own OSD, safe to destroy now (OVERLAY_RGN, synchronous operation)
	printf("Destroying OSD for all VENC channels...\n");
	for (int i = 0; i < venc_count; ++i) {
		venc_destroy_osd(i);  // Destroy OSD for VENC channel i
	}
	printf("All VENC OSD destroyed\n");

	// Step 5: Unbind all connections
	printf("Unbinding all connections...\n");
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

	// Step 6: Close VO
	printf("Closing VO...\n");
	for (int i = 0; i < active; ++i) {
		RK_MPI_VO_DisableChn(g_VoLayer, i);
	}
	RK_MPI_VO_DisableLayer(g_VoLayer);
	RK_MPI_VO_Disable(g_VoDev);
	RK_MPI_VO_UnBindLayer(g_VoLayer, g_VoDev);
	RK_MPI_VO_CloseFd();
	printf("VO closed\n");

	// Step 7: Destroy VENC channels (already stopped and joined)
	printf("Destroying VENC channels...\n");
	for (int i = 0; i < venc_count; ++i) {
		RK_MPI_VENC_DestroyChn(i);
	}

	// Destroy JPEG encoder
	if (g_enable_snapshot) {
		RK_MPI_VENC_DestroyChn(VENC_CHN_JPEG);
		printf("JPEG encoder destroyed\n");
	}
	printf("VENC channels destroyed\n");

	// Step 8: Stop and destroy VPSS
	printf("Stopping VPSS...\n");
	for (int i = 0; i < num_channels; ++i) {
		if (g_chnInfo[i].isActive) {
			RK_MPI_VPSS_StopGrp(i);
			for (int ch = 0; ch < 5; ++ch) {
				RK_MPI_VPSS_DisableChn(i, ch);
			}
			RK_MPI_VPSS_DestroyGrp(i);
		}
	}
	printf("VPSS stopped\n");

	// Step 9: Close VI channels
	printf("Closing VI...\n");
	for (int i = 0; i < num_channels; ++i) {
		RK_MPI_VI_DisableChn(s32PipeId, i);
	}
	printf("VI closed\n");

cleanup:
	printf("System cleanup complete\n");
	RK_MPI_SYS_Exit();
	return 0;
}
