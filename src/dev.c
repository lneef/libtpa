/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2022, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#include <stdio.h>
#include <string.h>

#include <rte_malloc.h>

#include "dev.h"
#include "ip.h"
#include "log.h"
#include "neigh.h"
#include "sock.h"
#include "tpa.h"

struct net_dev dev;

static uint32_t cfg_mtu;
static struct cfg_spec net_cfg_specs[] = {
    {
        .name = "net.ip",
        .type = CFG_TYPE_IPV4,
        .data = &dev.ip4,
        .flags = CFG_FLAG_RDONLY,
    },
    {
        .name = "net.mask",
        .type = CFG_TYPE_MASK,
        .data = &dev.mask,
        .flags = CFG_FLAG_RDONLY,
    },
    {
        .name = "net.gw",
        .type = CFG_TYPE_IPV4,
        .data = &dev.gw4,
        .flags = CFG_FLAG_RDONLY,
    },
    {
        .name = "net.ip6",
        .type = CFG_TYPE_IPV6,
        .data = &dev.ip6,
        .flags = CFG_FLAG_RDONLY,
    },
    {
        .name = "net.gw6",
        .type = CFG_TYPE_IPV6,
        .data = &dev.gw6,
        .flags = CFG_FLAG_RDONLY,
    },
    {
        .name = "net.mac",
        .type = CFG_TYPE_MAC,
        .data = &dev.mac,
        .data_len = sizeof(dev.mac),
        .flags = CFG_FLAG_RDONLY,
    },
    {
        .name = "net.mtu",
        .type = CFG_TYPE_UINT,
        .data = &cfg_mtu,
        .flags = CFG_FLAG_RDONLY | CFG_FLAG_HAS_MIN | CFG_FLAG_HAS_MAX,
        .min = RTE_ETHER_MIN_MTU,
        .max = JUMBO_MTU,
    }};

int net_dev_init_early(void) {
  memset(&dev, 0, sizeof(dev));

  cfg_spec_register(net_cfg_specs, ARRAY_SIZE(net_cfg_specs));
  cfg_section_parse("net");

  dev.mtu = cfg_mtu ? cfg_mtu : DEFAULT_MTU;

  return 0;
}

int dev_port_init(void) {
  uint16_t i;
  uint64_t txq_size;
  uint64_t rxq_size;

  txq_size = tpa_cfg.nr_worker * sizeof(struct dev_txq);
  rxq_size = tpa_cfg.nr_worker * sizeof(struct dev_rxq);

  for (i = 0; i < dev.nr_port; i++) {
    dev.ports[i].port_id = i;
    dev.ports[i].nr_rx_burst = RX_BURST_SIZE;

    dev.ports[i].txq = rte_malloc(NULL, txq_size, 64);
    dev.ports[i].rxq = rte_malloc(NULL, rxq_size, 64);
    if (!dev.ports[i].txq || !dev.ports[i].rxq) {
      LOG_ERR("dev port queue malloc error");
      return -1;
    }

    memset(dev.ports[i].txq, 0, txq_size);
    memset(dev.ports[i].rxq, 0, rxq_size);

    rss_build_tag_table(dev.ports[i].rss_tags);

    rte_eth_dev_get_name_by_port(i, dev.ports[i].device_id);
    LOG("detected dpdk port %hu: %s", i, dev.ports[i].device_id);
  }

  return 0;
}

static void dev_mac_init(void) {
  if (rte_is_zero_ether_addr(&dev.mac)) {
    /*
     * XXX: if mac addr is not configured, set the mac to
     * the mac from port 0.
     */
    rte_eth_macaddr_get(0, &dev.mac);
  }
}

int net_dev_init(void) {
  assert(tpa_cfg.nr_dpdk_port <= MAX_PORT_NR);
  dev.nr_port = tpa_cfg.nr_dpdk_port;

  if (dev_port_init() == -1)
    return -1;
  dev_mac_init();

  return 0;
}
