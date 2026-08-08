/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AN7581_TUNNEL_UDF_H
#define AN7581_TUNNEL_UDF_H

#include "an7581/services/tunnel/bridge.h"
#include "an7581/services/tunnel/packet.h"

#define NPU_TUNNEL_UDF_OPERATION_COUNT_MAX 7U
#define NPU_TUNNEL_UDF_SRV6_HEADER_COUNT 8U
#define NPU_TUNNEL_UDF_TEMPLATE_STRIDE 128U
#define NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE 16U
#define NPU_TUNNEL_UDF_MAP_RECORD_SIZE 40U
#define NPU_TUNNEL_UDF_MAP_PACKET_PATCH_COUNT_MAX 3U
#define NPU_TUNNEL_UDF_MAP_PACKET_PATCH_SIZE_MAX 2U

struct npu_tunnel_udf_map_packet_patch {
  uint8_t data[NPU_TUNNEL_UDF_MAP_PACKET_PATCH_SIZE_MAX];
  uint16_t offset;
  uint8_t length;
};

struct npu_tunnel_udf_request {
  uint8_t *packet;
  size_t packet_extent;
  uint32_t packet_address;
  uint32_t template_base_address;
  const uint8_t *map_info_records;
  size_t map_info_extent;
  uint8_t *map_template_scratch;
  size_t map_template_scratch_extent;
  uint32_t map_template_scratch_address;
  uint16_t vxlan_mtu;
  uint8_t srv6_header_lengths[NPU_TUNNEL_UDF_SRV6_HEADER_COUNT];
  uint8_t channel;
};

struct npu_tunnel_udf_operation_sequence {
  struct npu_tunnel_packet_operation
      operations[NPU_TUNNEL_UDF_OPERATION_COUNT_MAX];
  struct npu_tunnel_udf_map_packet_patch
      map_packet_patches[NPU_TUNNEL_UDF_MAP_PACKET_PATCH_COUNT_MAX];
  uint8_t map_template[NPU_TUNNEL_UDF_MAP_RECORD_SIZE];
  size_t operation_count;
  size_t map_packet_patch_count;
  uint16_t received_length;
  uint16_t srv6_destination_offset;
  uint16_t srv6_segment_offset;
  uint16_t srv6_segments_left_offset;
  uint16_t ipv6_next_header_offset;
  uint16_t ipv6_payload_length_offset;
  uint16_t ipv6_payload_length;
  uint8_t udf;
  uint8_t layer_2_offset;
  uint8_t srv6_segments_left;
  uint8_t ipv6_next_header;
  uint8_t map_template_length;
  bool srv6_segment_advance;
  bool srv6_segments_left_write;
  bool srv6_header_remove;
};

bool npu_tunnel_udf_operations_plan(
    const struct npu_tunnel_udf_request *request,
    struct npu_tunnel_udf_operation_sequence *sequence);
enum npu_runtime_result
npu_tunnel_udf_execute(const struct npu_tunnel_udf_request *request,
                       const struct npu_tunnel_bridge_backend *bridge);

#endif
