/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_MAILBOX_H
#define AN7581_TUNNEL_MAILBOX_H

#include "an7581/platform/types.h"

#define NPU_TUNNEL_MAILBOX_HEADER_SIZE 8U
#define NPU_TUNNEL_BRIDGE_CONTEXT_COUNT 8U
#define NPU_TUNNEL_BRIDGE_CONTEXT_SIZE 50U
#define NPU_TUNNEL_FRAGMENT_MTU_COUNT 4U
#define NPU_TUNNEL_SRV6_HEADER_COUNT 8U
#define NPU_TUNNEL_SRV6_HEADER_CAPACITY 128U
#define NPU_TUNNEL_LOCAL_SRV6_ADDRESS_SIZE 16U

enum npu_tunnel_command {
  NPU_TUNNEL_STORE_BRIDGE_CONTEXT = 0,
  NPU_TUNNEL_NOOP,
  NPU_TUNNEL_SET_VXLAN_MTU,
  NPU_TUNNEL_STORE_SRV6_HEADER,
  NPU_TUNNEL_SET_LOCAL_SRV6_ADDRESS,
  NPU_TUNNEL_SET_FRAGMENT_MTU,
  NPU_TUNNEL_DEBUG_CONTROL,
  NPU_TUNNEL_SET_MAP_INFO_BASE,
};

enum npu_tunnel_debug_action {
  NPU_TUNNEL_DEBUG_DUMP_BRIDGE_COUNTERS = 0,
  NPU_TUNNEL_DEBUG_ENABLE_BRIDGE,
  NPU_TUNNEL_DEBUG_FLUSH_REASSEMBLY,
};

struct npu_tunnel_bridge_context {
  uint8_t data[NPU_TUNNEL_BRIDGE_CONTEXT_SIZE];
  bool valid;
};

struct npu_tunnel_srv6_header {
  uint8_t data[NPU_TUNNEL_SRV6_HEADER_CAPACITY];
  uint8_t length;
  bool valid;
};

struct npu_tunnel_fragment_mtu {
  uint32_t value;
  bool valid;
};

struct npu_tunnel_debug_state {
  uint32_t last_action;
  uint32_t last_reserved;
  uint32_t bridge_dump_requests;
  uint32_t reassembly_flush_requests;
  bool bridge_enabled;
  bool last_request_valid;
};

struct npu_tunnel_state {
  struct npu_tunnel_bridge_context
      bridge_context[NPU_TUNNEL_BRIDGE_CONTEXT_COUNT];
  struct npu_tunnel_fragment_mtu fragment_mtu[NPU_TUNNEL_FRAGMENT_MTU_COUNT];
  struct npu_tunnel_srv6_header srv6_header[NPU_TUNNEL_SRV6_HEADER_COUNT];
  struct npu_tunnel_debug_state debug;
  uint8_t local_srv6_address[NPU_TUNNEL_LOCAL_SRV6_ADDRESS_SIZE];
  uint32_t map_info_base;
  uint32_t vxlan_mtu;
  uint32_t last_command;
  uint32_t decoded_requests;
  uint32_t invalid_requests;
  uint32_t successful_requests;
  uint32_t rejected_requests;
  bool local_srv6_address_valid;
  bool map_info_base_valid;
  bool vxlan_mtu_valid;
  bool last_command_valid;
};

bool npu_tunnel_mailbox_handle(struct npu_tunnel_state *state, void *buffer,
                               size_t length);

#endif
