/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_CORE7_DISPATCH_H
#define AN7581_CORE7_DISPATCH_H

#include "an7581/platform/tr471_runtime_dispatch.h"
#include "an7581/platform/tunnel.h"

#define AN7581_CORE7_HART UINT32_C(7)
#define AN7581_CORE7_TUNNEL_CHANNEL UINT8_C(7)

struct an7581_core7_dispatch {
  struct an7581_tr471_runtime_dispatch *tr471;
  struct an7581_tunnel_platform *volatile tunnel;
  volatile bool tunnel_published;
  bool initialized;
};

struct an7581_core7_dispatch_result {
  struct an7581_tr471_runtime_dispatch_result tr471;
  struct npu_tunnel_packet_ingress_result tunnel;
  enum npu_runtime_result tunnel_status;
  enum npu_runtime_result status;
  bool tr471_polled;
  bool tunnel_polled;
  bool tunnel_suppressed;
  bool waiting_for_runtime;
  bool should_backoff;
};

enum npu_runtime_result
an7581_core7_dispatch_initialize(struct an7581_core7_dispatch *dispatch,
                                 struct an7581_tr471_runtime_dispatch *tr471);
enum npu_runtime_result
an7581_core7_dispatch_publish_tunnel(struct an7581_core7_dispatch *dispatch,
                                     struct an7581_tunnel_platform *tunnel);
enum npu_runtime_result
an7581_core7_dispatch_step(struct an7581_core7_dispatch *dispatch,
                           uint32_t hart_id,
                           struct an7581_core7_dispatch_result *result);

#endif
