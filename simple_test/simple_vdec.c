#include "rk_comm_vdec.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_mmz.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vdec.h"
#include "rk_type.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_VDEC_CHN 0
#define DEFAULT_FILE_PATH "test.h264"
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
static bool quit = false;
static RK_CODEC_ID_E codec_id = RK_VIDEO_ID_AVC;

static unsigned h264_get_next_frame_size(int fd, unsigned char nal[5], int *eos) {
	int frameType = 0;
	int isNalu = 0;
	int isFrame = 0;
	unsigned size = 0;
	off_t filesize;

	off_t pos = lseek(fd, 0, SEEK_CUR);
	filesize = lseek(fd, 0, SEEK_END);
	lseek(fd, pos, SEEK_SET);

	while (1) {
		nal[0] = nal[1];
		nal[1] = nal[2];
		nal[2] = nal[3];
		nal[3] = nal[4];
		if (read(fd, &nal[4], 1) != 1) {
			if (lseek(fd, 0, SEEK_CUR) == filesize)
				*eos = 1;
			else
				*eos = 0;
			lseek(fd, pos, SEEK_SET);
			return size;
		}
		size++;

		if (isNalu) {
			isNalu = 0;
			frameType = nal[4] & 0x1F;
			if (frameType == 5 || frameType == 1)
				isFrame = 1;
		}

		if ((nal[0] != 0x00 && nal[1] == 0x00 && nal[2] == 0x00 && nal[3] == 0x01) ||
		    (nal[1] == 0x00 && nal[2] == 0x00 && nal[3] == 0x00 && nal[4] == 0x01)) {
			if (isFrame) {
				size = nal[3] == 0x01 ? size - 3 : size - 4;
				if (lseek(fd, 0, SEEK_CUR) == filesize)
					*eos = 1;
				else
					*eos = 0;
				lseek(fd, pos, SEEK_SET);
				return size;
			} else {
				isNalu = 1;
			}
		}
	}

	return 0;
}

static unsigned h265_get_next_frame_size(int fd, unsigned char nal[5], int *eos) {
	int frameType = 0;
	int isNalu = 0;
	int isFrame = 0;
	unsigned size = 0;
	off_t filesize;

	off_t pos = lseek(fd, 0, SEEK_CUR);
	filesize = lseek(fd, 0, SEEK_END);
	lseek(fd, pos, SEEK_SET);

	while (1) {
		nal[0] = nal[1];
		nal[1] = nal[2];
		nal[2] = nal[3];
		nal[3] = nal[4];
		if (read(fd, &nal[4], 1) != 1) {
			if (lseek(fd, 0, SEEK_CUR) == filesize)
				*eos = 1;
			else
				*eos = 0;
			lseek(fd, pos, SEEK_SET);
			return size;
		}
		size++;

		if (isNalu) {
			if (!frameType) {
				frameType = nal[4] << 8;
				continue;
			} else {
				isNalu = 0;
				frameType |= nal[4];
			}
			if (frameType == 0x2601 || frameType == 0x0201)
				isFrame = 1;
		}

		if ((nal[0] != 0x00 && nal[1] == 0x00 && nal[2] == 0x00 && nal[3] == 0x01) ||
		    (nal[1] == 0x00 && nal[2] == 0x00 && nal[3] == 0x00 && nal[4] == 0x01)) {
			if (isFrame) {
				size = nal[3] == 0x01 ? size - 3 : size - 4;
				if (lseek(fd, 0, SEEK_CUR) == filesize)
					*eos = 1;
				else
					*eos = 0;
				lseek(fd, pos, SEEK_SET);
				return size;
			} else {
				frameType = 0;
				isNalu = 1;
			}
		}
	}

	return 0;
}

static int mjpeg_get_next_frame_size(int fd) {
	off_t start_pos = lseek(fd, 0, SEEK_CUR);
	if (start_pos < 0) {
		RK_LOGE("lseek failed: %s", strerror(errno));
		return -1;
	}

	RK_U8 buffer[4] = {0};
	int length = 0;
	int bytes_count = 0;
	ssize_t bytes_read;

	while (1) {
		bytes_read = read(fd, &buffer[bytes_count], 1);
		if (bytes_read != 1) {
			if (bytes_read == 0 && bytes_count >= 2 && buffer[bytes_count - 2] == 0xFF &&
			    buffer[bytes_count - 1] == 0xD9) {
				lseek(fd, start_pos, SEEK_SET);
				return length;
			}
			lseek(fd, start_pos, SEEK_SET);
			return -1;
		}

		length++;
		bytes_count++;
		if (bytes_count >= 4) {
			if (buffer[0] == 0xFF && buffer[1] == 0xD9 && buffer[2] == 0xFF &&
			    buffer[3] == 0xD8) {
				lseek(fd, start_pos, SEEK_SET);
				return length - 2;
			}
			memmove(buffer, buffer + 1, 3);
			bytes_count = 3;
		}
	}

	return -1;
}

void *send_stream(void *arg) {
	char *file_path = (char *)arg;
	VDEC_STREAM_S vdec_stream;
	MB_BLK mb_blk;
	int fd;
	unsigned char nal[5] = {0, 0, 0, 0, 0};
	int eos = 0;
	int size = 0;

	fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		printf("%s not exist!\n", file_path);
		exit(-1);
	}

	while (1) {
		if (codec_id == RK_VIDEO_ID_AVC)
			size = h264_get_next_frame_size(fd, nal, &eos);
		else if (codec_id == RK_VIDEO_ID_HEVC)
			size = h265_get_next_frame_size(fd, nal, &eos);
		else if (codec_id == RK_VIDEO_ID_MJPEG || codec_id == RK_VIDEO_ID_JPEG)
			size = mjpeg_get_next_frame_size(fd);
		if (size <= 0)
			break;
		RK_S32 ret = RK_MPI_MMZ_Alloc(&mb_blk, size, 0);
		assert(ret == RK_SUCCESS);
		read(fd, RK_MPI_MB_Handle2VirAddr(mb_blk), size);
		vdec_stream.pMbBlk = mb_blk;
		vdec_stream.u32Len = size;
		vdec_stream.u64PTS = 0;
		vdec_stream.bEndOfStream = RK_FALSE;
		ret = RK_MPI_VDEC_SendStream(DEFAULT_VDEC_CHN, &vdec_stream, -1);

		RK_MPI_MMZ_Free(mb_blk);
	}

	if (eos) {
		size = 1024;
		RK_S32 ret = RK_MPI_MMZ_Alloc(&mb_blk, size, 0);
		assert(ret == RK_SUCCESS);
		vdec_stream.pMbBlk = mb_blk;
		vdec_stream.u32Len = size;
		vdec_stream.u64PTS = 0;
		vdec_stream.bEndOfStream = RK_TRUE;
		ret = RK_MPI_VDEC_SendStream(DEFAULT_VDEC_CHN, &vdec_stream, -1);

		RK_MPI_MMZ_Free(mb_blk);
	}

	close(fd);
	pthread_exit(NULL);
}

RK_S32 dump_frame(VIDEO_FRAME_INFO_S *pstFrame, FILE *fp) {
	RK_S32 s32Ret = RK_SUCCESS;
	RK_U32 i;
	RK_U32 width = 0;
	RK_U32 height = 0;
	RK_U32 h_stride = 0;
	RK_U32 v_stride = 0;
	RK_U8 *base_y = RK_NULL;
	RK_U8 *base_c = RK_NULL;
	RK_U8 *base_u = RK_NULL;
	RK_U8 *base_v = RK_NULL;
	RK_U8 *base = RK_NULL;

	if (RK_NULL == fp || RK_NULL == pstFrame) {
		RK_LOGV("no file(%p) or frame(%p)", fp, pstFrame);
		return RK_FAILURE;
	}

	base = (RK_U8 *)RK_MPI_MB_Handle2VirAddr(pstFrame->stVFrame.pMbBlk);
	RK_MPI_SYS_MmzFlushCache(pstFrame->stVFrame.pMbBlk, RK_TRUE);
	width = pstFrame->stVFrame.u32Width;
	height = pstFrame->stVFrame.u32Height;
	h_stride = pstFrame->stVFrame.u32VirWidth;
	v_stride = pstFrame->stVFrame.u32VirHeight;

	if (COMPRESS_AFBC_16x16 == pstFrame->stVFrame.enCompressMode) {
		RK_U64 bufSize = RK_MPI_MB_GetSize(pstFrame->stVFrame.pMbBlk);
		fwrite(base, 1, bufSize, fp);
		return RK_SUCCESS;
	}

	switch (pstFrame->stVFrame.enPixelFormat) {
	case RK_FMT_YUV420SP_VU:
	case RK_FMT_YUV420SP: {
		base_y = base;
		base_c = base + h_stride * v_stride;
		for (i = 0; i < height; i++, base_y += h_stride) {
			fwrite(base_y, 1, width, fp);
		}
		for (i = 0; i < height / 2; i++, base_c += h_stride) {
			fwrite(base_c, 1, width, fp);
		}
		fflush(fp);
	} break;

	case RK_FMT_YUV420P: {
		base_y = base;
		base_u = base + h_stride * v_stride;
		base_v = base + h_stride * v_stride * 5 / 4;
		for (i = 0; i < height; i++, base_y += h_stride) {
			fwrite(base_y, 1, width, fp);
		}
		for (i = 0; i < height / 4; i++, base_u += h_stride) {
			fwrite(base_u, 1, width, fp);
		}
		for (i = 0; i < height / 4; i++, base_v += h_stride) {
			fwrite(base_v, 1, width, fp);
		}
		fflush(fp);
	} break;

	case RK_FMT_RGB565:
	case RK_FMT_BGR565: {
		base_y = base;
		h_stride = pstFrame->stVFrame.u32VirWidth * 2;

		for (i = 0; i < height; i++, base_y += h_stride)
			fwrite(base_y, 1, width * 2, fp);

		fflush(fp);
	} break;

	case RK_FMT_RGB888:
	case RK_FMT_BGR888: {
		base_y = base;
		h_stride = pstFrame->stVFrame.u32VirWidth * 3;

		for (i = 0; i < height; i++, base_y += h_stride)
			fwrite(base_y, 1, width * 3, fp);

		fflush(fp);
	} break;

	default: {
		s32Ret = RK_FAILURE;
		RK_LOGE("not supported format %d", pstFrame->stVFrame.enPixelFormat);
	} break;
	}

	return s32Ret;
}

void *get_frame(void *arg) {
	char *file_path = (char *)arg;
	int ret;
	RK_S32 s32FrameCount = 0;
	FILE *fp = NULL;
	VIDEO_FRAME_INFO_S sFrame;

	if (file_path) {
		fp = fopen(file_path, "wb");
		if (fp) {
			RK_LOGI("open file %s success", file_path);
		} else {
			RK_LOGE("open file %s failed[%d, %s]", file_path, errno, strerror(errno));
		}
	}

	memset(&sFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
	while (1) {
		ret = RK_MPI_VDEC_GetFrame(DEFAULT_VDEC_CHN, &sFrame, 1000);
		if (ret == RK_SUCCESS) {
			s32FrameCount++;
			RK_LOGI("get chn %d frame %d", DEFAULT_VDEC_CHN, s32FrameCount);
			if ((sFrame.stVFrame.u32FrameFlag & FRAME_FLAG_SNAP_END) ==
			    FRAME_FLAG_SNAP_END) {
				RK_MPI_VDEC_ReleaseFrame(DEFAULT_VDEC_CHN, &sFrame);
				RK_LOGI("chn %d reach eos frame.", DEFAULT_VDEC_CHN);
				break;
			}
			dump_frame(&sFrame, fp);
			RK_MPI_VDEC_ReleaseFrame(DEFAULT_VDEC_CHN, &sFrame);
		} else {
			if (quit && (codec_id == RK_VIDEO_ID_MJPEG || codec_id == RK_VIDEO_ID_JPEG))
				break;
		}
	}
	if (fp)
		fclose(fp);
	pthread_exit(NULL);
}

static void usage(char *name) {
	printf("Usage:\n");
	printf("\t%s -i /data/test_720P.h264\n", name);
	printf("\t%s -i /data/test_720P.h265\n", name);
	printf("\t%s -i /data/test_720P.mjpg -w 1280 -h 720\n", name);
	printf("Options: -o /tmp/output.yuv\n");
	exit(-1);
}

int main(int argc, char *argv[]) {
	int width = DEFAULT_WIDTH;
	int height = DEFAULT_HEIGHT;
	char *file_path = DEFAULT_FILE_PATH;
	char *output_path = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "w:h:i:o:")) != -1) {
		switch (opt) {
		case 'w':
			width = atoi(optarg);
			break;
		case 'h':
			height = atoi(optarg);
			break;
		case 'i':
			file_path = optarg;
			break;
		case 'o':
			output_path = optarg;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	printf("#file_path: %s\n", file_path);

	if (strstr(file_path, ".hevc") || strstr(file_path, ".h265") ||
	    strstr(file_path, ".265"))
		codec_id = RK_VIDEO_ID_HEVC;
	else if (strstr(file_path, ".264") || strstr(file_path, ".h264") ||
	         strstr(file_path, ".avc"))
		codec_id = RK_VIDEO_ID_AVC;
	else if (strstr(file_path, ".mjpeg") || strstr(file_path, ".mjpg"))
		codec_id = RK_VIDEO_ID_MJPEG;
	else if (strstr(file_path, ".jpeg") || strstr(file_path, ".jpg"))
		codec_id = RK_VIDEO_ID_JPEG;
	else {
		fprintf(stderr, "Unknown codec type for file: %s\n", file_path);
		usage(argv[0]);
	}

	RK_S32 ret = RK_MPI_SYS_Init();
	assert(ret == RK_SUCCESS);

	VDEC_CHN_ATTR_S vdec_chn_attr;
	memset(&vdec_chn_attr, 0, sizeof(VDEC_CHN_ATTR_S));
	vdec_chn_attr.enType = codec_id;
	vdec_chn_attr.enMode = VIDEO_MODE_FRAME;
	vdec_chn_attr.u32PicWidth = width;
	vdec_chn_attr.u32PicHeight = height;
	vdec_chn_attr.u32FrameBufCnt = 6;
	vdec_chn_attr.u32StreamBufCnt = 6;
	vdec_chn_attr.u32FrameBufDepth = 6;
	ret = RK_MPI_VDEC_CreateChn(DEFAULT_VDEC_CHN, &vdec_chn_attr);
	assert(ret == RK_SUCCESS);
	ret = RK_MPI_VDEC_StartRecvStream(DEFAULT_VDEC_CHN);
	assert(ret == RK_SUCCESS);

	pthread_t get_thread;
	ret = pthread_create(&get_thread, NULL, get_frame, output_path);
	assert(ret == 0);
	pthread_t send_thread;
	ret = pthread_create(&send_thread, NULL, send_stream, file_path);
	assert(ret == 0);

	ret = pthread_join(send_thread, NULL);
	quit = true;
	ret = pthread_join(get_thread, NULL);

	ret = RK_MPI_VDEC_StopRecvStream(DEFAULT_VDEC_CHN);
	ret = RK_MPI_VDEC_DestroyChn(DEFAULT_VDEC_CHN);

	ret = RK_MPI_SYS_Exit();

	return 0;
}