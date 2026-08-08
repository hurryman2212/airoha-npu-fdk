/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_PACKET_H
#define AN7581_TUNNEL_PACKET_H

#include "an7581/platform/types.h"

#define NPU_TUNNEL_PACKET_METADATA_SIZE 21U
#define NPU_TUNNEL_PACKET_CONTROL_SIZE 32U
#define NPU_TUNNEL_PACKET_MTU_OFFSET 18U
#define NPU_TUNNEL_PACKET_UDF_OFFSET 20U
#define NPU_TUNNEL_FRAGMENT_SEGMENT_COUNT 2U

enum npu_tunnel_ip_version {
  NPU_TUNNEL_IP_VERSION_UNKNOWN = 0,
  NPU_TUNNEL_IP_VERSION_4 = 4,
  NPU_TUNNEL_IP_VERSION_6 = 6,
};

enum npu_tunnel_packet_route {
  NPU_TUNNEL_PACKET_ROUTE_FRAGMENT = 0,
  NPU_TUNNEL_PACKET_ROUTE_REASSEMBLE,
  NPU_TUNNEL_PACKET_ROUTE_UDF_1_TO_40,
  NPU_TUNNEL_PACKET_ROUTE_UDF_41_TO_56,
  NPU_TUNNEL_PACKET_ROUTE_UDF_65_TO_68,
};

struct npu_tunnel_packet_classification {
  enum npu_tunnel_packet_route route;
  enum npu_tunnel_ip_version ip_version;
  uint16_t packet_length;
  uint16_t mtu;
  uint8_t layer_2_offset;
  uint8_t hop_flags;
  uint8_t udf;
  bool ip_version_ambiguous;
};

struct npu_tunnel_fragment_plan {
  enum npu_tunnel_ip_version ip_version;
  uint32_t mtu_boundary;
  uint16_t packet_length;
  uint16_t mtu;
  uint8_t layer_2_offset;
};

struct npu_tunnel_fragment_segment {
  uint16_t payload_length;
  uint16_t wire_length;
  uint16_t offset;
  bool more_fragments;
};

struct npu_tunnel_fragment_layout {
  enum npu_tunnel_ip_version ip_version;
  uint16_t original_payload_length;
  uint8_t network_header_length;
  uint8_t inserted_fragment_header_length;
  struct npu_tunnel_fragment_segment
      segments[NPU_TUNNEL_FRAGMENT_SEGMENT_COUNT];
};

bool npu_tunnel_packet_classify(
    const void *packet, size_t extent,
    struct npu_tunnel_packet_classification *classification);
bool npu_tunnel_fragment_plan(
    const struct npu_tunnel_packet_classification *classification,
    struct npu_tunnel_fragment_plan *plan);
bool npu_tunnel_fragment_layout(const struct npu_tunnel_fragment_plan *plan,
                                struct npu_tunnel_fragment_layout *layout);

#endif
