/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

#include "test_utils.h"

#define EXTBUF_SIZE		(1<<20)
#define EXTBUF_PGSZ		(1<<12)

/* external memory is not supported: registering it has to fail */
static void test_extmem_unsupported(void)
{
	void *addr;

	printf("%s\n", __func__);

	addr = mmap(NULL, EXTBUF_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(addr != MAP_FAILED);

	errno = 0;
	assert(tpa_extmem_register(addr, EXTBUF_SIZE, NULL,
				   EXTBUF_SIZE / EXTBUF_PGSZ, EXTBUF_PGSZ) == -1);
	assert(errno == ENOTSUP);

	errno = 0;
	assert(tpa_extmem_unregister(addr, EXTBUF_SIZE) == -1);
	assert(errno == ENOTSUP);

	assert(munmap(addr, EXTBUF_SIZE) == 0);
}

int main(int argc, char *argv[])
{
	ut_init(argc, argv);

	test_extmem_unsupported();

	return 0;
}
