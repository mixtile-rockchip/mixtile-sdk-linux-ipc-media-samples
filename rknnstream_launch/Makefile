# Makefile for RKNNStream Launch Applications
#
# Copyright (c) 2026 by Rockchip Electronics Co., Ltd. All Rights Reserved.

ifeq ($(COMPILE_FOR_BUILDROOT),)
ifeq ($(MEDIA_PARAM), )
    MEDIA_PARAM:=../Makefile.param
    include $(MEDIA_PARAM)
endif
endif

ifneq ($(V),)
export CMD_DBG=
else
export CMD_DBG=@
endif

CURRENT_DIR := $(shell pwd)

# RKNNStream库路径
RKNS_PATH ?= $(RK_MEDIA_TOP_DIR)/rknnstream
RKNS_BUILD ?= $(RKNS_PATH)/build
RKNS_INSTALL ?= $(RK_MEDIA_OUTPUT)

# 头文件路径
RKNS_INC_FLAGS := -I$(RKNS_PATH)/include
RKNS_INC_FLAGS += -I$(RKNS_PATH)/rkns_utils
RKNS_INC_FLAGS += -I$(CURRENT_DIR)/yaml
RKNS_INC_FLAGS += -I$(RKNS_INSTALL)/include

# 库文件路径
RKNS_LD_FLAGS := -L$(RKNS_BUILD)
RKNS_LD_FLAGS += -L$(RKNS_INSTALL)/lib
RKNS_LD_FLAGS += -lrkns -lrockchip_osal -lrockx2 -lrockx_modules_open
RKNS_LD_FLAGS += -lrknn3_api -lrknnrt -lrockx_aicore -lrknn3_api_native -lrknn3_api_rkcp
RKNS_LD_FLAGS += -lrockit
RKNS_LD_FLAGS += -lpthread -lm
RKNS_LD_FLAGS += -Wl,-rpath-link,$(RKNS_INSTALL)/lib -Wl,--allow-shlib-undefined

# 编译选项
RKNS_CFLAGS := -Wall -Wextra -g -fPIC $(RKNS_INC_FLAGS)

ifeq ($(COMPILE_FOR_BUILDROOT),y)
SAMPLE_CC := $(CC)
else
SAMPLE_CC := $(RK_MEDIA_CROSS)-gcc
endif

# 源文件
YAML_SRC := $(CURRENT_DIR)/yaml/yaml_parse.c
LAUNCH_SRC := $(CURRENT_DIR)/rkns_launch.c
LAUNCH_CONFIG_SRC := $(CURRENT_DIR)/rkns_launch_config.c
ALL_LAUNCH_SRC := $(LAUNCH_SRC) $(LAUNCH_CONFIG_SRC) $(YAML_SRC)

# 目标文件
LAUNCH_BIN := rkns-launch-1.0

PKG_BIN ?= out

all: $(LAUNCH_BIN)

$(LAUNCH_BIN): $(ALL_LAUNCH_SRC)
	$(CMD_DBG)$(SAMPLE_CC) $(RKNS_CFLAGS) -o $@ $(ALL_LAUNCH_SRC) $(RKNS_LD_FLAGS)

install:
	$(CMD_DBG)mkdir -p $(CURRENT_DIR)/$(PKG_BIN)/bin
	$(CMD_DBG)cp -f $(LAUNCH_BIN) $(CURRENT_DIR)/$(PKG_BIN)/bin/
	$(CMD_DBG)cp -f $(CURRENT_DIR)/rkns_launch_config.yaml $(CURRENT_DIR)/$(PKG_BIN)/bin/

clean:
	$(CMD_DBG)rm -f $(LAUNCH_BIN) *.o yaml/*.o

distclean: clean
	$(CMD_DBG)rm -rf $(PKG_BIN)

.PHONY: all install clean distclean
