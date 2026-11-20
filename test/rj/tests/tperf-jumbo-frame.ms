# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2023, ByteDance Ltd. and/or its Affiliates
# Author: Tao Liu <liutao.xyz@bytedance.com>

params:
  server_nr_thread: [ 1 ]
  client_nr_thread: [ 1 ]
  duration: [ 5s ]

  ip: [ "127.0.0.1", "::1" ]

  test: [ rr ]
  size: [ 16KB ]

  mtu: [ 9000, 4000, 1500 ]
  client_mss: [ 0, 512, 1750, 4096, 8192 ]
  server_mss: [ 0, 512, 1750, 4096, 8192 ]
end

#include tperf-common.msh

cfg()
{
	SERVER_TPA_CFG="net { mtu=$mtu; } tcp { usr_snd_mss=$client_mss; }"
	CLIENT_TPA_CFG="net { mtu=$mtu; } tcp { usr_snd_mss=$server_mss; }"
}

verify()
{
	detect_count0_error
}

cfg
run
verify
