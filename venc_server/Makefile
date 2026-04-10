# Rockchip venc Makefile
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

VENC_CC := $(RK_MEDIA_CROSS)-gcc
VENC_AR := $(RK_MEDIA_CROSS)-ar
ifneq ($(RK_MEDIA_CC),)
VENC_CC := $(RK_MEDIA_CC)
VENC_AR := $(RK_MEDIA_AR)
endif

CURRENT_DIR := $(shell pwd)
VENC_OUT_DIR :=$(CURRENT_DIR)/out

VENC_PKG_CONF_OPTS += -DRKPLATFORM=ON

ifeq ($(RK_MEDIA_CHIP), rv1126)
VENC_PKG_CONF_OPTS += -DCMAKE_SYSTEM_PROCESSOR=armv7l
VENC_PKG_CONF_OPTS += -DARCH64=OFF
endif

VENC_VO_LD_FLAGS := -lrockit
ifeq ($(RK_MEDIA_CHIP), rv1126b)
VENC_LD_FLAGS += -DRV1126B
VENC_VO_LD_FLAGS += -DRV1126B
VENC_SPECIAL_SRC_DIR := rv1103_rv1106_rv1103b_rv1126b
endif

ifeq ($(RK_MEDIA_CHIP), rv1106)
VENC_VO_LD_FLAGS = -lrockit_full
VENC_PKG_CONF_OPTS += -DARCH64=OFF
VENC_LD_FLAGS += -DRV1106_RV1103
VENC_VO_LD_FLAGS += -DRV1106_RV1103
VENC_SPECIAL_SRC_DIR := rv1103_rv1106_rv1103b_rv1126b
endif

ifeq ($(RK_MEDIA_CHIP), rv1103b)
VENC_PKG_CONF_OPTS += -DCMAKE_SYSTEM_PROCESSOR=armv7l
VENC_PKG_CONF_OPTS += -DARCH64=OFF
VENC_VO_LD_FLAGS = -lrockit_full
VENC_LD_FLAGS += -DRV1103B
VENC_VO_LD_FLAGS += -DRV1103B
VENC_SPECIAL_SRC_DIR := rv1103_rv1106_rv1103b_rv1126b
endif

VENC_OPTS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib

INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include
ifeq ($(RK_MEDIA_CHIP), rv1126)
VENC_LD_FLAGS += -DUAPI
VENC_LD_FLAGS += -DRV1126_RV1109
else
VENC_LD_FLAGS += -DUAPI2
endif

VENC_CFLAGS += -g -Wall $(INC_FLAGS) $(VENC_PKG_CONF_OPTS) $(RK_MEDIA_CROSS_CFLAGS)

VENC_LD_FLAGS += $(VENC_OPTS) -L$(RK_MEDIA_OUTPUT)/lib -L$(RK_MEDIA_OUTPUT)/root/usr/lib \
					-lpthread -lm -lrockit
VENC_LD_FLAGS += -Wl,--gc-sections -Wl,--as-needed

VENC_CFLAGS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib -Wl,-rpath-link,$(RK_MEDIA_OUTPUT)/root/usr/lib

VENC_VO_LD_FLAGS += $(RK_MEDIA_OPTS) -L$(RK_MEDIA_OUTPUT)/lib -L$(RK_MEDIA_OUTPUT)/root/usr/lib -Wl,-Bdynamic \
						-lrockchip_mpp -lrga -ldrm -lpthread

SOURCES_C     = venc_server.c
ifneq ($(VENC_SPECIAL_SRC_DIR),)
SPEICAL_SOURCES_C = $(wildcard $(strip $(VENC_SPECIAL_SRC_DIR))/*.c)
endif

OBJECTS       = $(basename $(SOURCES_C)) $(basename $(SPEICAL_SOURCES_C))

$(warning "here add the debug info $(OBJECTS)")
all:
	$(CMD_DBG)test -f $(RK_MEDIA_OUTPUT)/lib/librockit.so  || make -C $(RK_MEDIA_TOP_DIR) media_libs
	$(CMD_DBG)$(VENC_CC) $(VENC_CFLAGS) venc_server.c cJSON.c -o venc_server $(VENC_LD_FLAGS)
	$(CMD_DBG)make venc_build

venc_build: $(OBJECTS)
	$(CMD_DBG)mkdir -p $(VENC_OUT_DIR)/bin
	$(CMD_DBG)mv -f $(OBJECTS) $(VENC_OUT_DIR)/bin

	$(call MAROC_COPY_PKG_TO_MEDIA_OUTPUT, $(RK_MEDIA_OUTPUT)/bin, $(VENC_OUT_DIR)/bin)

clean:
	$(CMD_DBG)echo "clean venc"
	$(CMD_DBG)rm -rf $(VENC_OUT_DIR)
