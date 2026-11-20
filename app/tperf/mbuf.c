/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2022, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#include <stdio.h>
#include <sys/mman.h>

#include "tperf.h"

#define HUGE_PAGE_SIZE		(2 << 20)

/*
 * For zero copy write, the NIC reads the mbufs by their IO address, which
 * is the physical one without an IOMMU: the pages have to be there and
 * stay. The kernel doesn't migrate hugepages; it may migrate locked 4K
 * pages, which are therefore only good with an IOMMU.
 */
static void *zwrite_mem_alloc(size_t *page_size)
{
	void *addr;

	*page_size = HUGE_PAGE_SIZE;
	addr = mmap(NULL, MBUF_POOL_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);
	if (addr != MAP_FAILED)
		return addr;

	fprintf(stderr, "warn: no hugepages for the mbuf pool; using locked 4K pages\n");

	*page_size = MBUF_SIZE;
	addr = mmap(NULL, MBUF_POOL_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED, -1, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "failed to allocate the locked mbuf pool: %s\n", strerror(errno));
		exit(1);
	}

	return addr;
}

/* returns the IO address of each page */
static uint64_t *zwrite_mem_setup(void *addr, size_t page_size)
{
	size_t nr_page = MBUF_POOL_SIZE / page_size;
	uint64_t *iovas;
	size_t start;
	size_t i;

	iovas = zmalloc_assert(nr_page * sizeof(uint64_t));
	for (i = 0; i < nr_page; i++) {
		iovas[i] = tpa_extmem_virt2iova((char *)addr + i * page_size);
		if (iovas[i] == 0) {
			fprintf(stderr, "err_tpa_extmem_virt2iova: %s\n", strerror(errno));
			exit(1);
		}
	}

	if (tpa_extmem_register(addr, MBUF_POOL_SIZE, iovas, nr_page, page_size) != 0) {
		fprintf(stderr, "err_tpa_extmem_register: %s\n", strerror(errno));
		exit(1);
	}

	/* map every run of pages that is contiguous in the IO address space */
	start = 0;
	for (i = 1; i <= nr_page; i++) {
		if (i < nr_page && iovas[i] == iovas[i - 1] + page_size)
			continue;

		/* ENOTSUP: no IOMMU to program (say, igb_uio); nothing to map then */
		if (tpa_extmem_map((char *)addr + start * page_size, iovas[start],
				   (i - start) * page_size) != 0 && errno != ENOTSUP) {
			fprintf(stderr, "err_tpa_extmem_map: %s; hugepages keep the number of mappings low\n",
				strerror(errno));
			exit(1);
		}

		start = i;
	}

	return iovas;
}

struct mbuf_pool *mbuf_pool_create(void)
{
	struct mbuf_pool *pool;
	struct mbuf *mbufs;
	uint64_t *iovas = NULL;
	size_t page_size = MBUF_SIZE;
	size_t nr_mbuf;
	size_t off;
	void *addr;
	int i;

	if (ctx.enable_zwrite) {
		addr = zwrite_mem_alloc(&page_size);
		iovas = zwrite_mem_setup(addr, page_size);
	} else {
		addr = mmap(NULL, MBUF_POOL_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		assert(addr != MAP_FAILED);
	}

	nr_mbuf = MBUF_POOL_SIZE / MBUF_SIZE;
	pool = zmalloc_assert(sizeof(struct mbuf_pool) + sizeof(void *) * nr_mbuf);
	pool->nr_mbuf = nr_mbuf;

	mbufs = zmalloc_assert(sizeof(struct mbuf) * nr_mbuf);
	for (i = 0; i < nr_mbuf; i++) {
		mbufs[i].refcnt = 0;
		mbufs[i].pool = pool;
		mbufs[i].data = addr + i * MBUF_SIZE;

		if (iovas) {
			off = (size_t)i * MBUF_SIZE;
			mbufs[i].iova = iovas[off / page_size] + off % page_size;
		}

		mbuf_free(&mbufs[i]);
	}

	free(iovas);

	return pool;
}
