# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2021-2023, ByteDance Ltd. and/or its Affiliates
# Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>

CFLAGS += -I$(SRC_ROOT)/include
CFLAGS += $(DPDK_CFLAGS)

LDFLAGS += $(LIBTPA_A) -lm
LDFLAGS += $(DPDK_LDFLAGS)

OBJ_DIR = $(OBJ_ROOT)/tools
BIN_DIR = $(BIN_ROOT)/tools

OBJS = $(SRCS:%.c=$(OBJ_DIR)/%.o)
DEPS = $(SRCS:%.c=$(OBJ_DIR)/%.d)

BIN = $(BIN_DIR)/$(APP)

all: $(BIN) scripts

$(BIN): $(OBJS) | OUT_DIRS
	$(Q)echo "  LD $(notdir $@)"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS)
ifeq ($(BUILD_MODE),release)
	$(Q)echo "  ST $(notdir $@)"
	$(Q)strip $@
endif

scripts:
ifneq ($(SCRIPTS),)
	$(Q)cp -d $(SCRIPTS) $(BIN_DIR)
endif

$(OBJS): $(OBJ_DIR)/%.o: %.c | OUT_DIRS
	$(Q)echo "  CC $(notdir $@)"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

OUT_DIRS:
	$(Q)mkdir -p $(OBJ_DIR) $(BIN_DIR)
