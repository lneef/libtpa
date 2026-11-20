/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 * Author: Kai Xiong <xiongkai.123@bytedance.com>
 */
#include <netinet/icmp6.h>

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "tpa.h"
#include "lib/utils.h"
#include "log.h"
#include "dev.h"
#include "shell.h"
#include "packet.h"
#include "neigh.h"

static void get_mulitcast_ip(uint8_t *dst, uint8_t *target)
{
	uint8_t ip[16] = {
		0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00,
	};

	ip[13] = target[13];
	ip[14] = target[14];
	ip[15] = target[15];

	memcpy(dst, ip, sizeof(ip));
}

static inline void get_multicast_mac(struct rte_ether_addr *addr, uint8_t *ip)
{
	uint16_t *ip16 = (uint16_t *)ip;
	uint16_t *addr16 = (uint16_t *)addr->addr_bytes;

	addr16[0] = 0x3333;
	addr16[1] = ip16[6];
	addr16[2] = ip16[7];
}

struct ndp_solicit_hdr {
	struct rte_ether_hdr eth;
	struct rte_ipv6_hdr ip6;
	struct nd_neighbor_solicit ns;
	struct nd_opt_hdr opt;
	struct rte_ether_addr mac;
} __attribute__((packed));

static void ndp_init_hdr(struct ndp_solicit_hdr *hdr, struct tpa_ip *ip)
{
	struct rte_ipv6_hdr *ip_hdr;
	struct nd_neighbor_solicit *ns;
	uint8_t multicast_ip[16];

	get_mulitcast_ip(multicast_ip, ip->u8);

	rte_ether_addr_copy(&dev.mac, ETH_SRC_ADDR(&hdr->eth));
	get_multicast_mac(ETH_DST_ADDR(&hdr->eth), multicast_ip);
	hdr->eth.ether_type = htons(RTE_ETHER_TYPE_IPV6);

	ip_hdr = &hdr->ip6;
	memcpy(IP6_ADDR_BYTES(ip_hdr->dst_addr), multicast_ip, 16);
	memcpy(IP6_ADDR_BYTES(ip_hdr->src_addr), &dev.ip6.ip, 16);
	ip_hdr->vtc_flow = htonl(6 << 28);
	ip_hdr->payload_len = htons(sizeof(*hdr) - sizeof(hdr->eth) - sizeof(hdr->ip6));
	ip_hdr->hop_limits = 255;
	ip_hdr->proto = IPPROTO_ICMPV6;

	ns = &hdr->ns;
	ns->nd_ns_hdr.icmp6_type = ND_NEIGHBOR_SOLICIT;
	ns->nd_ns_hdr.icmp6_code = 0;
	memcpy(&ns->nd_ns_target, ip, 16);

	hdr->opt.nd_opt_type = ND_OPT_SOURCE_LINKADDR;
	hdr->opt.nd_opt_len = 1;
	rte_ether_addr_copy(&dev.mac, &hdr->mac);

	ns->nd_ns_hdr.icmp6_cksum = 0;
	ns->nd_ns_hdr.icmp6_cksum = rte_ipv6_udptcp_cksum(ip_hdr, ns);
}

static int ndp_solicit(struct tpa_ip *ip, struct tpa_worker *worker)
{
	struct ndp_solicit_hdr *hdr;
	struct packet *pkt;
	int ret;

	pkt = packet_alloc(generic_pkt_pool);
	if (pkt == NULL)
		return -ERR_PKT_ALLOC_FAIL;

	hdr = (struct ndp_solicit_hdr *)rte_pktmbuf_append(&pkt->mbuf, sizeof(*hdr));
	if (!hdr)
		return -ERR_PKT_PREPEND_HDR;

	ndp_init_hdr(hdr, ip);

	ret = dev_port_txq_enqueue(0, worker->queue, pkt);
	if (unlikely(ret < 0))
		packet_free(pkt);

	return ret;
}

int ndp_input(uint8_t *packet, size_t len)
{
	struct nd_neighbor_advert *na  = (struct nd_neighbor_advert *)packet;
	struct nd_opt_hdr *opt;
	struct tpa_ip ip;

	if (len < sizeof(*na) + sizeof(struct nd_opt_hdr) + RTE_ETHER_ADDR_LEN)
		return 0;

	if (na->nd_na_hdr.icmp6_type == ND_NEIGHBOR_ADVERT &&
	    na->nd_na_hdr.icmp6_code == 0) {
		opt = (struct nd_opt_hdr *)(na + 1);

		if (opt->nd_opt_type == ND_OPT_TARGET_LINKADDR && opt->nd_opt_len == 1) {
			tpa_ip_set_ipv6(&ip, (uint8_t *)(&na->nd_na_target));
			neigh_input(&ip, (uint8_t *)(opt + 1));
		}
	}

	return 0;
}

static void ndp_init(void)
{
	/* for supporting loopback mode */
	neigh_update(&dev.ip6.ip, dev.mac.addr_bytes);
}

const struct neigh_ops ndp_ops = {
	.nd_init = ndp_init,
	.nd_solicit = ndp_solicit,
};
