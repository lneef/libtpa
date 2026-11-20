/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2022, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#include <stdio.h>

#include "test_utils.h"
#include "port_alloc.h"
#include "rss.h"

#define NR_BIND		4096

static struct tpa_ip remote_ip;
static struct tpa_ip local_ip;
static struct tcp_sock dummy_tsock;

static void key_init(struct sock_key *key)
{
	memset(key, 0, sizeof(*key));
	sock_key_init(key, &remote_ip, 80, &local_ip, 0);
}

/* the port has to hash to the queue of the worker that binds it */
static void assert_port_on_worker_queue(uint16_t port)
{
	uint32_t hash;

	hash = rss_hash_4tuple(rss_default_key_adapted, tpa_ip_get_ipv4(&remote_ip), 80,
			       tpa_ip_get_ipv4(&local_ip), port);
	assert(rss_bucket(hash, RSS_TAG_TABLE_SIZE) % tpa_cfg.nr_worker == worker->queue);
}

static void test_port_bind_basic(void)
{
	struct sock_key key;
	uint16_t port;

	printf("testing %s ...\n", __func__);

	key_init(&key);
	port = port_bind_on_rss_port(worker, &key, &dummy_tsock);
	assert(port != 0);
	assert(key.local_port == port);
	assert(sock_table_lookup(&worker->sock_table, &key) == &dummy_tsock);
	assert_port_on_worker_queue(port);

	assert(port_unbind(worker, &key) == 0);
	assert(sock_table_lookup(&worker->sock_table, &key) == NULL);
	assert(port_unbind(worker, &key) == -1);
}

static void test_port_bind_unique(void)
{
	static uint8_t seen[1 << 16];
	static uint16_t ports[NR_BIND];
	struct sock_key key;
	int i;

	printf("testing %s ...\n", __func__);

	for (i = 0; i < NR_BIND; i++) {
		key_init(&key);
		ports[i] = port_bind_on_rss_port(worker, &key, &dummy_tsock);
		assert(ports[i] != 0);
		assert(seen[ports[i]] == 0);
		seen[ports[i]] = 1;
		assert_port_on_worker_queue(ports[i]);
	}

	for (i = 0; i < NR_BIND; i++) {
		key_init(&key);
		key.local_port = ports[i];
		assert(port_unbind(worker, &key) == 0);
		seen[ports[i]] = 0;
	}
}

int main(int argc, char *argv[])
{
	int i;

	ut_init(argc, argv);

	tpa_ip_set_ipv4(&remote_ip, 0x0a000021);
	tpa_ip_set_ipv4(&local_ip, 0x0a000022);

	test_port_bind_basic();

	/* bind, unbind and bind again */
	for (i = 0; i < 4; i++)
		test_port_bind_unique();

	return 0;
}
