/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_FRAGMENT_H
#define AN7581_TUNNEL_FRAGMENT_H

#include "an7581/services/tunnel/bridge.h"
#include "an7581/services/tunnel/packet.h"

#define NPU_TUNNEL_FRAGMENT_OPERATION_COUNT_MAX 6U
#define NPU_TUNNEL_IPV6_FRAGMENT_NEXT_HEADER 44U
#define NPU_TUNNEL_PPPOE_LENGTH_OFFSET_DIRECT 0x32U
#define NPU_TUNNEL_PPPOE_LENGTH_OFFSET_VLAN 0x36U

struct npu_tunnel_fragment_operation_config {
  uint32_t packet_address;
  uint32_t ipv6_fragment_header_address;
  uint32_t ipv6_identification;
  uint8_t channel;
  uint8_t ipv6_next_header;
  uint8_t pppoe_length_offset;
};

struct npu_tunnel_fragment_operation_sequence {
  struct npu_tunnel_fragment_layout layout;
  struct npu_tunnel_packet_operation
      operations[NPU_TUNNEL_FRAGMENT_OPERATION_COUNT_MAX];
  size_t operation_count;
  uint16_t ipv6_next_header_offset;
  uint8_t ipv6_next_header_value;
  bool ipv6_next_header_write;
};

struct npu_tunnel_fragment_runtime {
  uint32_t ipv6_identification;
};

struct npu_tunnel_fragment_execution {
  uint8_t *packet;
  size_t packet_extent;
  uint32_t packet_address;
  uint32_t ipv6_fragment_header_address;
  uint8_t channel;
  uint8_t pppoe_length_offset;
};

bool npu_tunnel_fragment_operations_plan(
    const struct npu_tunnel_fragment_plan *plan,
    const struct npu_tunnel_fragment_operation_config *config,
    struct npu_tunnel_fragment_operation_sequence *sequence);
enum npu_runtime_result npu_tunnel_fragment_execute(
    struct npu_tunnel_fragment_runtime *runtime,
    const struct npu_tunnel_bridge_backend *bridge,
    const struct npu_tunnel_fragment_plan *plan,
    const struct npu_tunnel_fragment_execution *execution);

#endif
