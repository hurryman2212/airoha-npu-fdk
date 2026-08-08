/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_RUNTIME_H
#define AN7581_TUNNEL_RUNTIME_H

#include "an7581/services/tunnel/fragment.h"
#include "an7581/services/tunnel/mailbox.h"
#include "an7581/services/tunnel/reassembly.h"
#include "an7581/services/tunnel/udf.h"

struct npu_tunnel_map_info_binding {
  const uint8_t *records;
  size_t record_extent;
  uint8_t *template_scratch;
  size_t template_scratch_extent;
  uint32_t physical_base;
  uint32_t template_scratch_address;
};

struct npu_tunnel_packet_runtime_config {
  const struct npu_tunnel_bridge_backend *bridge;
  const struct npu_tunnel_state *mailbox_state;
  npu_tunnel_reassembly_release_fn release;
  void *release_context;
  struct npu_tunnel_map_info_binding map_info;
  uint32_t ipv6_fragment_header_address;
  uint32_t udf_template_base_address;
};

struct npu_tunnel_packet_runtime {
  struct npu_tunnel_fragment_runtime fragment;
  struct npu_tunnel_reassembly_runtime reassembly;
  struct npu_tunnel_packet_runtime_config config;
  uint32_t reassembly_flush_requests_consumed;
};

struct npu_tunnel_packet_request {
  uint8_t *packet;
  size_t packet_extent;
  uint32_t packet_address;
  uint8_t channel;
};

enum npu_tunnel_packet_disposition {
  NPU_TUNNEL_PACKET_DISPOSITION_UNKNOWN = 0,
  NPU_TUNNEL_PACKET_DISPOSITION_CALLER,
  NPU_TUNNEL_PACKET_DISPOSITION_RELEASED,
  NPU_TUNNEL_PACKET_DISPOSITION_BRIDGE,
  NPU_TUNNEL_PACKET_DISPOSITION_REASSEMBLY_PENDING,
};

struct npu_tunnel_packet_dispatch {
  struct npu_tunnel_packet_classification classification;
  enum npu_tunnel_packet_disposition disposition;
};

struct npu_tunnel_packet_ingress_result {
  struct npu_tunnel_bridge_ingress_packet packet;
  struct npu_tunnel_packet_dispatch dispatch;
  enum npu_runtime_result dispatch_result;
  enum npu_runtime_result release_result;
};

bool npu_tunnel_packet_runtime_initialize(
    struct npu_tunnel_packet_runtime *runtime,
    const struct npu_tunnel_packet_runtime_config *config);
enum npu_runtime_result npu_tunnel_packet_runtime_synchronize(
    struct npu_tunnel_packet_runtime *runtime);
enum npu_runtime_result npu_tunnel_packet_runtime_execute(
    struct npu_tunnel_packet_runtime *runtime,
    const struct npu_tunnel_packet_request *request,
    struct npu_tunnel_packet_dispatch *dispatch);
enum npu_runtime_result npu_tunnel_packet_runtime_ingress_step(
    struct npu_tunnel_packet_runtime *runtime,
    struct npu_tunnel_bridge_ingress *ingress, uint8_t channel,
    struct npu_tunnel_packet_ingress_result *result);

#endif
