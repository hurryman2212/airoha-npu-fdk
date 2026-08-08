/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_REASSEMBLY_H
#define AN7581_TUNNEL_REASSEMBLY_H

#include "an7581/services/tunnel/bridge.h"
#include "an7581/services/tunnel/packet.h"

#define NPU_TUNNEL_REASSEMBLY_OPERATION_COUNT_MAX 3U

enum npu_tunnel_reassembly_action {
  NPU_TUNNEL_REASSEMBLY_STORE_IPV4 = 0,
  NPU_TUNNEL_REASSEMBLY_COMPLETE_IPV4,
  NPU_TUNNEL_REASSEMBLY_STORE_IPV6,
  NPU_TUNNEL_REASSEMBLY_COMPLETE_IPV6,
};

typedef enum npu_runtime_result (*npu_tunnel_reassembly_release_fn)(
    void *context, uint8_t channel, uint32_t packet_address,
    uint16_t received_length);

struct npu_tunnel_reassembly_packet {
  uint8_t *packet;
  size_t packet_extent;
  uint32_t packet_address;
  uint8_t channel;
};

struct npu_tunnel_reassembly_pending {
  uint8_t *packet;
  size_t packet_extent;
  uint32_t packet_address;
  uint16_t received_length;
  uint8_t layer_2_offset;
  uint8_t channel;
  bool active;
};

struct npu_tunnel_reassembly_runtime {
  struct npu_tunnel_reassembly_pending ipv4;
  struct npu_tunnel_reassembly_pending ipv6;
  npu_tunnel_reassembly_release_fn release;
  void *release_context;
};

struct npu_tunnel_reassembly_operation_sequence {
  struct npu_tunnel_packet_operation
      operations[NPU_TUNNEL_REASSEMBLY_OPERATION_COUNT_MAX];
  size_t operation_count;
  enum npu_tunnel_reassembly_action action;
  uint32_t packet_control;
  uint16_t received_length;
  uint16_t current_fragment_field_offset;
  uint16_t current_fragment_field;
  uint16_t current_next_header_offset;
  uint16_t pending_length_offset;
  uint16_t pending_length;
  uint8_t current_next_header;
  uint8_t layer_2_offset;
  bool current_fragment_field_write;
  bool current_next_header_write;
  bool pending_length_write;
  bool replaces_pending;
};

bool npu_tunnel_reassembly_runtime_initialize(
    struct npu_tunnel_reassembly_runtime *runtime,
    npu_tunnel_reassembly_release_fn release, void *release_context);
bool npu_tunnel_reassembly_operations_plan(
    const struct npu_tunnel_reassembly_runtime *runtime,
    const struct npu_tunnel_reassembly_packet *packet,
    struct npu_tunnel_reassembly_operation_sequence *sequence);
enum npu_runtime_result npu_tunnel_reassembly_execute(
    struct npu_tunnel_reassembly_runtime *runtime,
    const struct npu_tunnel_bridge_backend *bridge,
    const struct npu_tunnel_reassembly_packet *packet);
enum npu_runtime_result
npu_tunnel_reassembly_flush(struct npu_tunnel_reassembly_runtime *runtime);

#endif
