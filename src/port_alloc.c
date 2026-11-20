/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2023, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#include <assert.h>
#include <stdint.h>

#include <rte_random.h>

#include "dev.h"
#include "port_alloc.h"
#include "rss.h"
#include "sock_table.h"
#include "worker.h"

/*
 * Hash the tuple as the NIC sees it on incoming traffic: src is the
 * remote side, dst is us. The tag is XORed into the local (dst) port.
 */
static uint16_t worker_refine_rss_port(struct tpa_worker *worker, uint16_t port,
                                       struct sock_key *key,
                                       uint16_t local_port) {
  int tag;
  uint32_t hash = rss_hash_4tuple(
      rss_default_key_adapted, tpa_ip_get_ipv4(&key->remote_ip),
      key->remote_port, tpa_ip_get_ipv4(&key->local_ip), local_port);
  tag = rss_get_tag_for_queue(dev.ports[port].rss_tags, hash, worker->queue,
                              tpa_cfg.nr_worker);
  assert(tag >= 0);
  return (uint16_t)(local_port ^ tag);
}

/* returns 0 on failure */
uint16_t port_bind_on_rss_port(struct tpa_worker *worker, struct sock_key *key,
                               struct tcp_sock *tsock) {
  uint16_t port;
  int nr_try = 0;

  /*
   * a given local port can't be steered to our queue; always pick one.
   * The port is fully owned by DPDK, so the whole port space is ours.
   */
  while (nr_try++ < UINT16_MAX) {
    port = (rte_rand() % UINT16_MAX) + 1;
    port = worker_refine_rss_port(worker, tsock->port_id, key, port);
    if (port == 0)
      continue;

    key->local_port = port;
    if (sock_table_lookup(&worker->sock_table, key))
      continue;

    if (sock_table_add(&worker->sock_table, key, tsock))
      return 0;
    return port;
  }

  return 0;
}

int port_unbind(struct tpa_worker *worker, struct sock_key *key) {
  if (sock_table_del(&worker->sock_table, key) < 0)
    return -1;
  return 0;
}
