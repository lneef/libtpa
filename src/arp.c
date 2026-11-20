/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 * Author: Kai Xiong <xiongkai.123@bytedance.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_arp.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "dev.h"
#include "lib/utils.h"
#include "log.h"
#include "neigh.h"
#include "packet.h"
#include "shell.h"
#include "tpa.h"

struct arp_solicit_hdr {
  struct rte_ether_hdr eth;
  struct rte_arp_hdr arp;
  char pad[22];
} __attribute__((packed));

static struct rte_ether_addr broadcast_mac = {
    .addr_bytes = "\xff\xff\xff\xff\xff\xff",
};

static void arp_init_hdr(struct arp_solicit_hdr *hdr, struct tpa_ip *ip) {
  struct rte_ether_hdr *eth;
  struct rte_arp_hdr *arp;

  eth = &hdr->eth;
  rte_ether_addr_copy(&dev.mac, ETH_SRC_ADDR(eth));
  rte_ether_addr_copy(&broadcast_mac, ETH_DST_ADDR(eth));
  eth->ether_type = htons(RTE_ETHER_TYPE_ARP);

  arp = &hdr->arp;
  arp->arp_hardware = htons(RTE_ARP_HRD_ETHER);
  arp->arp_protocol = htons(RTE_ETHER_TYPE_IPV4);
  arp->arp_hlen = sizeof(struct rte_ether_addr);
  arp->arp_plen = sizeof(struct in_addr);
  arp->arp_opcode = htons(RTE_ARP_OP_REQUEST);

  rte_ether_addr_copy(&dev.mac, &arp->arp_data.arp_sha);
  rte_ether_addr_copy(&broadcast_mac, &arp->arp_data.arp_tha);
  arp->arp_data.arp_sip = dev.ip4;
  arp->arp_data.arp_tip = tpa_ip_get_ipv4(ip);
}

static int arp_solicit(struct tpa_ip *ip, struct tpa_worker *worker) {
  struct arp_solicit_hdr *hdr;
  struct packet *pkt;
  int ret;

  pkt = packet_alloc(generic_pkt_pool);
  if (pkt == NULL)
    return -ERR_PKT_ALLOC_FAIL;

  hdr = (struct arp_solicit_hdr *)rte_pktmbuf_append(&pkt->mbuf, sizeof(*hdr));
  if (!hdr) {
    packet_free(pkt);
    return -ERR_PKT_PREPEND_HDR;
  }

  arp_init_hdr(hdr, ip);

  ret = dev_port_txq_enqueue(0, worker->queue, pkt);
  if (unlikely(ret < 0))
    packet_free(pkt);

  return ret;
}

int arp_input(uint8_t *pkt, size_t len) {
  struct rte_arp_hdr *arp;
  struct tpa_ip ip;

  arp = (struct rte_arp_hdr *)(pkt + sizeof(struct rte_ether_hdr));

  tpa_ip_set_ipv4(&ip, arp->arp_data.arp_sip);
  neigh_input(&ip, arp->arp_data.arp_sha.addr_bytes);

  return 0;
}

static int arp_cache_init_from_env(const char *entries) {
  union {
    uint32_t raw;
    uint8_t bytes[4];
  } ip4;
  struct tpa_ip ip;
  uint8_t mac[6];
  char *saveptr;
  char *entry;
  char *dup;
  int nr_entry = 0;
  int ret = 0;

  dup = strdup(entries);
  if (!dup)
    return -1;

  for (entry = strtok_r(dup, ",", &saveptr); entry;
       entry = strtok_r(NULL, ",", &saveptr)) {
    if (sscanf(entry, " %hhu.%hhu.%hhu.%hhu=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &ip4.bytes[0], &ip4.bytes[1], &ip4.bytes[2], &ip4.bytes[3],
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 10) {
      LOG_ERR("invalid TPA_ARP_ENTRIES entry: %s", entry);
      ret = -1;
      break;
    }

    neigh_update(tpa_ip_set_ipv4(&ip, ip4.raw), mac);
    nr_entry += 1;
  }

  free(dup);
  LOG("populated %d neigh entries from TPA_ARP_ENTRIES", nr_entry);

  return ret;
}

static void arp_cache_init(void) {
  char *entries;

  entries = getenv("TPA_ARP_ENTRIES");
  if (entries)
    arp_cache_init_from_env(entries);
}

static int cmd_arp(struct shell_cmd_info *cmd) {
  neigh_dump(cmd->reply);

  return 0;
}

static const struct shell_cmd arp = {
    .name = "arp",
    .handler = cmd_arp,
};

static void arp_init(void) {
  if (dev.ip4 == 0)
    return;

  arp_cache_init();
  shell_register_cmd(&arp);
}

const struct neigh_ops arp_ops = {
    .nd_init = arp_init,
    .nd_solicit = arp_solicit,
};
