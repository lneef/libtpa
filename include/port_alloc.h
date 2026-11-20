/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2023, ByteDance Ltd. and/or its Affiliates
 * Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>
 */
#ifndef _PORT_ALLOC_H_
#define _PORT_ALLOC_H_

#include "ip.h"
#include "sock_table.h"

struct tpa_worker;
struct tcp_sock;
uint16_t port_bind_on_rss_port(struct tpa_worker *worker, struct sock_key *key,
                               struct tcp_sock *tsock);
int port_unbind(struct tpa_worker *worker, struct sock_key *key);

#endif
