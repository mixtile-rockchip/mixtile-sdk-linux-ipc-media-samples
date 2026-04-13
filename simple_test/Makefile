# Rockchip simple Makefile
ifeq ($(COMPILE_FOR_BUILDROOT),)
ifeq ($(MEDIA_PARAM), )
    MEDIA_PARAM:=../../Makefile.param
    include $(MEDIA_PARAM)
endif
endif

ifneq ($(V),)
export CMD_DBG=
else
export CMD_DBG=@
endif

SIMPLE_CC := $(RK_MEDIA_CROSS)-gcc
SIMPLE_AR := $(RK_MEDIA_CROSS)-ar
ifneq ($(RK_MEDIA_CC),)
SIMPLE_CC := $(RK_MEDIA_CC)
SIMPLE_AR := $(RK_MEDIA_AR)
endif

CURRENT_DIR := $(shell pwd)
SIMPLE_OUT_DIR :=$(CURRENT_DIR)/out

SIMPLE_PKG_CONF_OPTS += -DRKPLATFORM=ON

ifeq ($(RK_MEDIA_CHIP), rv1126)
SIMPLE_PKG_CONF_OPTS += -DCMAKE_SYSTEM_PROCESSOR=armv7l
SIMPLE_PKG_CONF_OPTS += -DARCH64=OFF
endif

SIMPLE_VO_LD_FLAGS := -lrockit
ifeq ($(RK_MEDIA_CHIP), rv1126b)
SIMPLE_LD_FLAGS += -DRV1126B
SIMPLE_VO_LD_FLAGS += -DRV1126B
SIMPLE_SPECIAL_SRC_DIR := rv1103_rv1106_rv1103b_rv1126b
endif

ifeq ($(RK_MEDIA_CHIP), rk3576)
SIMPLE_LD_FLAGS += -DRK3576
SIMPLE_VO_LD_FLAGS += -DRK3576
endif

ifeq ($(RK_MEDIA_CHIP), rk3572)
SIMPLE_LD_FLAGS += -DRK3572
SIMPLE_VO_LD_FLAGS += -DRK3572
endif

ifeq ($(RK_MEDIA_CHIP), rk3588)
SIMPLE_LD_FLAGS += -DRK3588
SIMPLE_VO_LD_FLAGS += -DRK3588
endif

ifeq ($(RK_MEDIA_CHIP), rv1106)
SIMPLE_VO_LD_FLAGS = -lrockit_full
SIMPLE_PKG_CONF_OPTS += -DARCH64=OFF
SIMPLE_LD_FLAGS += -DRV1106_RV1103
SIMPLE_VO_LD_FLAGS += -DRV1106_RV1103
SIMPLE_SPECIAL_SRC_DIR := rv1103_rv1106_rv1103b_rv1126b
endif

ifeq ($(RK_MEDIA_CHIP), rv1103b)
SIMPLE_PKG_CONF_OPTS += -DCMAKE_SYSTEM_PROCESSOR=armv7l
SIMPLE_PKG_CONF_OPTS += -DARCH64=OFF
SIMPLE_VO_LD_FLAGS = -lrockit_full
SIMPLE_LD_FLAGS += -DRV1103B
SIMPLE_VO_LD_FLAGS += -DRV1103B
SIMPLE_SPECIAL_SRC_DIR := rv1103_rv1106_rv1103b_rv1126b
endif

SIMPLE_OPTS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib

INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq
ifeq ($(RK_MEDIA_CHIP), rv1126)
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/uAPI
SIMPLE_LD_FLAGS += -DUAPI
SIMPLE_LD_FLAGS += -DRV1126_RV1109
else
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/uAPI2
SIMPLE_LD_FLAGS += -DUAPI2
endif

INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/common
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/xcore
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/algos
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/iq_parser
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/iq_parser_v2
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/smartIr
ifneq ($(filter y,$(CONFIG_RK_ISP_IRFPA) $(RK_ENABLE_IRFPA)),)
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rk_irfpa/simple_api
endif

SIMPLE_CFLAGS += -g -Wall $(INC_FLAGS) $(SIMPLE_PKG_CONF_OPTS) $(RK_MEDIA_CROSS_CFLAGS)

SIMPLE_LD_FLAGS += $(SIMPLE_OPTS) -L$(RK_MEDIA_OUTPUT)/lib -L$(RK_MEDIA_OUTPUT)/root/usr/lib \
					-lpthread -lm -lrockit \
					-lrockchip_mpp -DRKAIQ \
					-lrga
SIMPLE_LD_FLAGS += -Wl,--gc-sections -Wl,--as-needed

SIMPLE_CFLAGS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib -Wl,-rpath-link,$(RK_MEDIA_OUTPUT)/root/usr/lib

SIMPLE_VO_LD_FLAGS += $(RK_MEDIA_OPTS) -L$(RK_MEDIA_OUTPUT)/lib -L$(RK_MEDIA_OUTPUT)/root/usr/lib -Wl,-Bdynamic \
						-lrockchip_mpp -lrga -ldrm -lpthread

ifeq ($(RK_MEDIA_CHIP), rv1126)
SIMPLE_LD_FLAGS += -lasound
SIMPLE_VO_LD_FLAGS += -lasound
endif

SOURCES_C     = $(wildcard *.c)
ifneq ($(SIMPLE_SPECIAL_SRC_DIR),)
SPEICAL_SOURCES_C = $(wildcard $(strip $(SIMPLE_SPECIAL_SRC_DIR))/*.c)
endif

ifneq ($(filter y,$(CONFIG_RK_ISP_IRFPA) $(RK_ENABLE_IRFPA)),)
SPEICAL_SOURCES_C := $(SPEICAL_SOURCES_C) $(wildcard rv1126b/simple_irfpa_vi_bind_venc_rtsp.c)
endif

OBJECTS       = $(basename $(SOURCES_C)) $(basename $(SPEICAL_SOURCES_C))

$(warning "here add the debug info $(OBJECTS)")
all:
	$(CMD_DBG)test -f $(RK_MEDIA_OUTPUT)/lib/librockit.so  || make -C $(RK_MEDIA_TOP_DIR) media_libs
	$(CMD_DBG)make simple_build

simple_build: $(OBJECTS)
	$(CMD_DBG)mkdir -p $(SIMPLE_OUT_DIR)/bin
	$(CMD_DBG)mv -f $(OBJECTS) $(SIMPLE_OUT_DIR)/bin
	$(CMD_DBG)cp simple_test.sh simple_cmd.txt $(SIMPLE_OUT_DIR)/bin/

ifeq ($(RK_MEDIA_CHIP), rk3576)
	$(CMD_DBG)cp simple_cmd_rk3576.txt $(SIMPLE_OUT_DIR)/bin/simple_cmd.txt
endif

ifeq ($(RK_MEDIA_CHIP), rk3572)
	$(CMD_DBG)cp simple_cmd_rk3572.txt $(SIMPLE_OUT_DIR)/bin/simple_cmd.txt
endif

ifeq ($(RK_MEDIA_CHIP), rk3588)
	$(CMD_DBG)cp simple_cmd_rk3588.txt $(SIMPLE_OUT_DIR)/bin/simple_cmd.txt
endif

ifeq ($(RK_MEDIA_CHIP), rv1126b)
	$(CMD_DBG)cp simple_cmd_rv1126b.txt $(SIMPLE_OUT_DIR)/bin/simple_cmd.txt
	$(CMD_DBG)cp rk_2_1920x1080.json $(SIMPLE_OUT_DIR)/bin/
	$(CMD_DBG)cp rk_2_1920x1080.xml $(SIMPLE_OUT_DIR)/bin/
endif

	$(call MAROC_COPY_PKG_TO_MEDIA_OUTPUT, $(RK_MEDIA_OUTPUT)/bin, $(SIMPLE_OUT_DIR)/bin)

simple_vdec_bind_vpss_bind_vo: simple_vdec_bind_vpss_bind_vo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_vdec_bind_vo: simple_vdec_bind_vo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_vdec: simple_vdec.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_vo_send_frame: simple_vo_send_frame.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_adec_bind_ao: simple_adec_bind_ao.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_ai_bind_aenc: simple_ai_bind_aenc.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_adec_bind_ao_external_decoder: simple_adec_bind_ao_external_decoder.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaudio -lrkdemuxer

simple_ai_bind_aenc_external_encoder: simple_ai_bind_aenc_external_encoder.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaudio

simple_ai_get_frame: simple_ai_get_frame.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_ao_send_frame: simple_ao_send_frame.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_vi_bind_venc_jpeg: simple_vi_bind_venc_jpeg.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_vi_bind_venc_osd: simple_vi_bind_venc_osd.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_vi_get_frame: simple_vi_get_frame.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_vi_bind_ivs: simple_vi_bind_ivs.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

simple_vi_bind_venc: simple_vi_bind_venc.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

simple_vi_bind_venc_rtsp: simple_vi_bind_venc_rtsp.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq -lrtsp

simple_vi_bind_venc_rtsp_lowdelay: simple_vi_bind_venc_rtsp_lowdelay.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq -lrtsp

simple_vdec_bind_vo_rtsp_lowdelay: simple_vdec_bind_vo_rtsp_lowdelay.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS) -lrkdemuxer

simple_vi_bind_venc_svc_rtsp: simple_vi_bind_venc_svc_rtsp.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq -lrtsp

simple_vi_bind_vpss_bind_venc: simple_vi_bind_vpss_bind_venc.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

simple_vi_bind_avs_bind_venc: simple_vi_bind_avs_bind_venc.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS) -lrkaiq

simple_vi_get_frame_rkaiq: simple_vi_get_frame_rkaiq.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

simple_vi_get_frame_tde: simple_vi_get_frame_tde.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

simple_vi_bind_venc_rtsp_eptz: simple_vi_bind_venc_rtsp_eptz.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq -lrtsp

simple_vi_get_frame_send_vo: simple_vi_get_frame_send_vo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_vi_bind_vpss_bind_vo: simple_vi_bind_vpss_bind_vo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_vi_vpss_venc_rawstream: simple_vi_vpss_venc_rawstream.c
	@$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

simple_vi_avs_send_vo: simple_vi_avs_send_vo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

simple_multi_ahd_vpss_test: simple_multi_ahd_vpss_test.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

rv1126b/simple_irfpa_vi_bind_venc_rtsp: rv1126b/simple_irfpa_vi_bind_venc_rtsp.c
	$(CMD_DBG)echo "build $@"
ifneq ($(filter y,$(CONFIG_RK_ISP_IRFPA) $(RK_ENABLE_IRFPA)),)
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkirfpa -lrtsp
endif

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_chn_sharebuf: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_chn_sharebuf.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_change_resolution: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_change_resolution.c
	$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_rtsp_dev_chn_sharebuf: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_rtsp_dev_chn_sharebuf.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq -lrtsp

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_rtsp_three_camera: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_rtsp_three_camera.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq -lrtsp

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_wrap: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_wrap.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS)

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_combo: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_combo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

$(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_rawstream_wrap: $(SIMPLE_SPECIAL_SRC_DIR)/simple_vi_bind_venc_rawstream_wrap.c
	@$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) -lrkaiq

simple_vi_gdc_vo: simple_vi_gdc_vo.c
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_VO_LD_FLAGS)

clean:
	$(CMD_DBG)echo "clean simple"
	$(CMD_DBG)rm -rf $(SIMPLE_OUT_DIR)
