/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/packet.h"

#include "an7581/runtime/endian.h"

#define NPU_TUNNEL_LAYER_2_OFFSET_SHIFT 20U
#define NPU_TUNNEL_LAYER_2_OFFSET_MASK UINT32_C(0x7f)
#define NPU_TUNNEL_IPV4_FLAG (UINT32_C(1) << 27)
#define NPU_TUNNEL_IPV6_FLAG (UINT32_C(1) << 28)
#define NPU_TUNNEL_HOP_FLAGS_SHIFT 28U
#define NPU_TUNNEL_HOP_FLAGS_MASK UINT32_C(0x7)
#define NPU_TUNNEL_IPV4_HEADER_SIZE 20U
#define NPU_TUNNEL_IPV6_HEADER_SIZE 40U
#define NPU_TUNNEL_IPV6_FRAGMENT_HEADER_SIZE 8U
#define NPU_TUNNEL_FRAGMENT_ALIGNMENT 8U

static void
classify_ip_version(uint32_t metadata,
                    struct npu_tunnel_packet_classification *classification) {
  bool ipv4;
  bool ipv6;

  ipv4 = (metadata & NPU_TUNNEL_IPV4_FLAG) != 0U;
  ipv6 = (metadata & NPU_TUNNEL_IPV6_FLAG) != 0U;
  classification->ip_version_ambiguous = ipv4 == ipv6;
  if (ipv4 && !ipv6)
    classification->ip_version = NPU_TUNNEL_IP_VERSION_4;
  else if (ipv6 && !ipv4)
    classification->ip_version = NPU_TUNNEL_IP_VERSION_6;
  else
    classification->ip_version = NPU_TUNNEL_IP_VERSION_UNKNOWN;
}

static bool
classify_route(struct npu_tunnel_packet_classification *classification) {
  if (classification->hop_flags == 1U) {
    classification->route = NPU_TUNNEL_PACKET_ROUTE_FRAGMENT;
    return true;
  }
  if (classification->hop_flags == 2U) {
    classification->route = NPU_TUNNEL_PACKET_ROUTE_REASSEMBLE;
    return true;
  }
  if (classification->udf >= 1U && classification->udf <= 40U) {
    classification->route = NPU_TUNNEL_PACKET_ROUTE_UDF_1_TO_40;
    return true;
  }
  if (classification->udf >= 41U && classification->udf <= 56U) {
    classification->route = NPU_TUNNEL_PACKET_ROUTE_UDF_41_TO_56;
    return true;
  }
  if (classification->udf >= 65U && classification->udf <= 68U) {
    classification->route = NPU_TUNNEL_PACKET_ROUTE_UDF_65_TO_68;
    return true;
  }
  return false;
}

bool npu_tunnel_packet_classify(
    const void *packet, size_t extent,
    struct npu_tunnel_packet_classification *classification) {
  struct npu_tunnel_packet_classification decoded = {0};
  const uint8_t *data = packet;
  uint32_t metadata;
  uint32_t route_word;

  if (packet == NULL || classification == NULL ||
      extent < NPU_TUNNEL_PACKET_METADATA_SIZE)
    return false;

  metadata = npu_load_little_endian_u32(data);
  route_word = npu_load_little_endian_u32(data + sizeof(uint32_t));
  decoded.packet_length = npu_load_little_endian_u16(data);
  if (decoded.packet_length < NPU_TUNNEL_PACKET_METADATA_SIZE ||
      decoded.packet_length > extent)
    return false;

  decoded.layer_2_offset =
      (uint8_t)((metadata >> NPU_TUNNEL_LAYER_2_OFFSET_SHIFT) &
                NPU_TUNNEL_LAYER_2_OFFSET_MASK);
  decoded.hop_flags = (uint8_t)((route_word >> NPU_TUNNEL_HOP_FLAGS_SHIFT) &
                                NPU_TUNNEL_HOP_FLAGS_MASK);
  decoded.mtu = npu_load_little_endian_u16(data + NPU_TUNNEL_PACKET_MTU_OFFSET);
  decoded.udf = data[NPU_TUNNEL_PACKET_UDF_OFFSET];
  classify_ip_version(metadata, &decoded);
  if (!classify_route(&decoded))
    return false;

  *classification = decoded;
  return true;
}

bool npu_tunnel_fragment_plan(
    const struct npu_tunnel_packet_classification *classification,
    struct npu_tunnel_fragment_plan *plan) {
  struct npu_tunnel_fragment_plan decoded = {0};

  if (classification == NULL || plan == NULL ||
      classification->route != NPU_TUNNEL_PACKET_ROUTE_FRAGMENT)
    return false;

  decoded.mtu_boundary =
      (uint32_t)classification->layer_2_offset + classification->mtu;
  if (decoded.mtu_boundary >= classification->packet_length ||
      classification->ip_version_ambiguous ||
      (classification->ip_version != NPU_TUNNEL_IP_VERSION_4 &&
       classification->ip_version != NPU_TUNNEL_IP_VERSION_6))
    return false;

  decoded.ip_version = classification->ip_version;
  decoded.packet_length = classification->packet_length;
  decoded.mtu = classification->mtu;
  decoded.layer_2_offset = classification->layer_2_offset;
  *plan = decoded;
  return true;
}

bool npu_tunnel_fragment_layout(const struct npu_tunnel_fragment_plan *plan,
                                struct npu_tunnel_fragment_layout *layout) {
  struct npu_tunnel_fragment_layout decoded = {0};
  uint32_t first_payload_capacity;
  uint32_t first_wire_length;
  uint32_t minimum_fragment_size;
  uint32_t original_payload_length;
  uint32_t second_payload_length;
  uint32_t second_wire_length;

  if (plan == NULL || layout == NULL ||
      plan->mtu_boundary != (uint32_t)plan->layer_2_offset + plan->mtu ||
      plan->mtu_boundary >= plan->packet_length)
    return false;

  decoded.ip_version = plan->ip_version;
  if (plan->ip_version == NPU_TUNNEL_IP_VERSION_4) {
    decoded.network_header_length = NPU_TUNNEL_IPV4_HEADER_SIZE;
  } else if (plan->ip_version == NPU_TUNNEL_IP_VERSION_6) {
    decoded.network_header_length = NPU_TUNNEL_IPV6_HEADER_SIZE;
    decoded.inserted_fragment_header_length =
        NPU_TUNNEL_IPV6_FRAGMENT_HEADER_SIZE;
  } else {
    return false;
  }

  minimum_fragment_size = (uint32_t)decoded.network_header_length +
                          decoded.inserted_fragment_header_length +
                          NPU_TUNNEL_FRAGMENT_ALIGNMENT;
  if (plan->mtu < minimum_fragment_size ||
      plan->packet_length <=
          (uint32_t)plan->layer_2_offset + decoded.network_header_length)
    return false;

  original_payload_length = (uint32_t)plan->packet_length -
                            plan->layer_2_offset -
                            decoded.network_header_length;
  first_payload_capacity =
      ((uint32_t)plan->mtu - decoded.network_header_length -
       decoded.inserted_fragment_header_length) &
      ~(NPU_TUNNEL_FRAGMENT_ALIGNMENT - 1U);
  if (first_payload_capacity == 0U ||
      first_payload_capacity >= original_payload_length)
    return false;

  second_payload_length = original_payload_length - first_payload_capacity;
  first_wire_length =
      (uint32_t)plan->layer_2_offset + decoded.network_header_length +
      decoded.inserted_fragment_header_length + first_payload_capacity;
  second_wire_length =
      (uint32_t)plan->layer_2_offset + decoded.network_header_length +
      decoded.inserted_fragment_header_length + second_payload_length;
  if (original_payload_length > UINT16_MAX ||
      first_payload_capacity > UINT16_MAX ||
      second_payload_length > UINT16_MAX || first_wire_length > UINT16_MAX ||
      second_wire_length > UINT16_MAX)
    return false;

  decoded.original_payload_length = (uint16_t)original_payload_length;
  decoded.segments[0].payload_length = (uint16_t)first_payload_capacity;
  decoded.segments[0].wire_length = (uint16_t)first_wire_length;
  decoded.segments[0].more_fragments = true;
  decoded.segments[1].payload_length = (uint16_t)second_payload_length;
  decoded.segments[1].wire_length = (uint16_t)second_wire_length;
  decoded.segments[1].offset = (uint16_t)first_payload_capacity;
  *layout = decoded;
  return true;
}
