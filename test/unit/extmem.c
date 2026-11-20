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
#define EXTBUF_NR_PAGE		(EXTBUF_SIZE / EXTBUF_PGSZ)

static void *extbuf_alloc(void)
{
	void *addr;

	addr = mmap(NULL, EXTBUF_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(addr != MAP_FAILED);

	return addr;
}

static void test_extmem_register(void)
{
	void *addr = extbuf_alloc();

	printf("%s\n", __func__);

	assert(tpa_extmem_register(addr, EXTBUF_SIZE, NULL, EXTBUF_NR_PAGE, EXTBUF_PGSZ) == 0);

	/* a region can be registered only once */
	errno = 0;
	assert(tpa_extmem_register(addr, EXTBUF_SIZE, NULL, EXTBUF_NR_PAGE, EXTBUF_PGSZ) == -1);
	assert(errno == EEXIST);

	assert(tpa_extmem_unregister(addr, EXTBUF_SIZE) == 0);

	errno = 0;
	assert(tpa_extmem_unregister(addr, EXTBUF_SIZE) == -1);
	assert(errno == ENOENT);

	assert(munmap(addr, EXTBUF_SIZE) == 0);
}

static void test_extmem_register_invalid(void)
{
	void *addr = extbuf_alloc();

	printf("%s\n", __func__);

	/* both the address and the length have to be page aligned */
	errno = 0;
	assert(tpa_extmem_register((char *)addr + 1, EXTBUF_SIZE - EXTBUF_PGSZ, NULL,
				   EXTBUF_NR_PAGE - 1, EXTBUF_PGSZ) == -1);
	assert(errno == EINVAL);

	errno = 0;
	assert(tpa_extmem_register(addr, EXTBUF_SIZE - 1, NULL,
				   EXTBUF_NR_PAGE, EXTBUF_PGSZ) == -1);
	assert(errno == EINVAL);

	assert(munmap(addr, EXTBUF_SIZE) == 0);
}

static void test_extmem_register_stress(void)
{
	void *addr;
	int i;

	printf("%s\n", __func__);

	for (i = 0; i < 30; i++) {
		addr = extbuf_alloc();

		assert(tpa_extmem_register(addr, EXTBUF_SIZE, NULL, EXTBUF_NR_PAGE, EXTBUF_PGSZ) == 0);
		assert(tpa_extmem_unregister(addr, EXTBUF_SIZE) == 0);

		assert(munmap(addr, EXTBUF_SIZE) == 0);
	}
}

/* there is no port in the unit test to map the memory for */
static void test_extmem_map_no_port(void)
{
	void *addr = extbuf_alloc();

	printf("%s\n", __func__);

	assert(tpa_extmem_register(addr, EXTBUF_SIZE, NULL, EXTBUF_NR_PAGE, EXTBUF_PGSZ) == 0);

	errno = 0;
	assert(tpa_extmem_map(addr, (uintptr_t)addr, EXTBUF_SIZE) == -1);
	assert(errno == ENODEV);

	errno = 0;
	assert(tpa_extmem_unmap(addr, (uintptr_t)addr, EXTBUF_SIZE) == -1);
	assert(errno == ENODEV);

	assert(tpa_extmem_unregister(addr, EXTBUF_SIZE) == 0);
	assert(munmap(addr, EXTBUF_SIZE) == 0);
}

/* the IO addresses given at register are the ones we get back */
static void test_extmem_virt2iova(void)
{
	uint64_t iovas[EXTBUF_NR_PAGE];
	char *addr = extbuf_alloc();
	int i;

	printf("%s\n", __func__);

	/* pages that are not contiguous in the IO address space */
	for (i = 0; i < EXTBUF_NR_PAGE; i++)
		iovas[i] = 0x100000000ull + (uint64_t)(EXTBUF_NR_PAGE - i) * 2 * EXTBUF_PGSZ;

	assert(tpa_extmem_register(addr, EXTBUF_SIZE, iovas, EXTBUF_NR_PAGE, EXTBUF_PGSZ) == 0);

	for (i = 0; i < EXTBUF_NR_PAGE; i++) {
		assert(tpa_extmem_virt2iova(addr + i * EXTBUF_PGSZ) == iovas[i]);
		assert(tpa_extmem_virt2iova(addr + i * EXTBUF_PGSZ + 123) == iovas[i] + 123);
	}

	assert(tpa_extmem_unregister(addr, EXTBUF_SIZE) == 0);
	assert(munmap(addr, EXTBUF_SIZE) == 0);
}

int main(int argc, char *argv[])
{
	ut_init(argc, argv);

	test_extmem_register();
	test_extmem_register_invalid();
	test_extmem_register_stress();
	test_extmem_map_no_port();
	test_extmem_virt2iova();

	return 0;
}
