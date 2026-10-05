# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2021-2023, ByteDance Ltd. and/or its Affiliates
# Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>

export SRC_ROOT   = $(shell pwd)
export BUILD_ROOT = $(SRC_ROOT)/build
export OBJ_ROOT   = $(BUILD_ROOT)/objs
export BIN_ROOT   = $(BUILD_ROOT)/bin
export INSTALL_ROOT = /usr/share/tpa
export LIBTPA_A  = $(BUILD_ROOT)/libtpa.a
export LIBTPA_SO = $(BUILD_ROOT)/libtpa.so

export CC ?= gcc

ifneq ($(V),)
export Q=
else
export Q=@
endif

ARCH ?= $(shell uname -m)
OS    = $(shell uname -o)

ifneq ($(MAKECMDGOALS),gtags)
ifneq ($(OS),GNU/Linux)
$(error libtpa builds only in GNU/Linux OS)
endif
endif

ifeq ($(ARCH),x86_64)
export RTE_TARGET = x86_64-native-linuxapp-gcc
else
export RTE_TARGET = arm64-bluefield-linux-gcc
endif

EXTRA_CFLAGS := -fPIC
ifeq ($(BUILD_MODE),release)
EXTRA_CFLAGS += -g -fno-omit-frame-pointer
else
EXTRA_CFLAGS += -g3 -O0
endif

CFLAGS := -O2 $(EXTRA_CFLAGS)
CFLAGS += -Wall -Werror -Wno-packed-not-aligned -Wno-format-truncation
CFLAGS += -Wno-address-of-packed-member

LDFLAGS := $(EXTRA_LDFLAGS)
LDFLAGS += -lpthread -ldl -lnuma -lpcap

ifeq ($(BUILD_MODE),asan)
CFLAGS  += -fsanitize=address 
LDFLAGS += -fsanitize=address
endif

export EXTRA_CFLAGS
export EXTRA_LDFLAGS
export CFLAGS
export LDFLAGS 

# dpdk is taken from the system install (pkg-config libdpdk); we link
# it statically, hence DPDK_LDFLAGS only reports the non-dpdk libs.
export DPDK_CFLAGS  := $(shell pkg-config --cflags libdpdk)
export DPDK_LDFLAGS := $(shell pkg-config --libs --static libdpdk | tr ' ' '\n' | \
			 grep -v rte | grep '^-l' | sort -u | tr '\n' ' ')
