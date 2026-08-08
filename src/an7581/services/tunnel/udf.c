/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/udf.h"

#include "an7581/runtime/endian.h"
#include "an7581/runtime/memory.h"

#define NPU_TUNNEL_UDF_LAYER_2_OFFSET_SHIFT 20U
#define NPU_TUNNEL_UDF_LAYER_2_OFFSET_MASK UINT32_C(0x7f)
#define NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_4_OFFSET 16U
#define NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_5_OFFSET 20U
#define NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_6_OFFSET 24U
#define NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_5 UINT32_C(0x7f4007ff)
#define NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_6 UINT32_C(0x0000ffff)
#define NPU_TUNNEL_UDF_PACKET_CONTROL_ROUTE UINT32_C(0x00003800)
#define NPU_TUNNEL_UDF_PACKET_CONTROL_CHANNEL_SHIFT 4U
#define NPU_TUNNEL_UDF_TEMPLATE_HEADER_SIZE 50U
#define NPU_TUNNEL_UDF_VXLAN_FRAGMENT_OVERHEAD 70U
#define NPU_TUNNEL_UDF_VXLAN_FRAGMENT_MINIMUM 78U
#define NPU_TUNNEL_UDF_FORWARD_REMOVE_SIZE 50U
#define NPU_TUNNEL_UDF_IPV4_HEADER_SOURCE_OFFSET 32U
#define NPU_TUNNEL_UDF_IPV4_FRAGMENT_HEADER_SIZE 34U
#define NPU_TUNNEL_UDF_IPV4_FRAGMENT_SOURCE_OVERHEAD 66U
#define NPU_TUNNEL_UDF_SRV6_TEMPLATE_FIRST 41U
#define NPU_TUNNEL_UDF_SRV6_TEMPLATE_LAST 48U
#define NPU_TUNNEL_UDF_SRV6_ADVANCE_FIRST 49U
#define NPU_TUNNEL_UDF_SRV6_ADVANCE_LAST 56U
#define NPU_TUNNEL_UDF_SRV6_NEXT_HEADER_OFFSET 0x26U
#define NPU_TUNNEL_UDF_IPV6_PAYLOAD_LENGTH_OFFSET 0x24U
#define NPU_TUNNEL_UDF_IPV6_DESTINATION_OFFSET 0x38U
#define NPU_TUNNEL_UDF_SRV6_HEADER_OFFSET 0x48U
#define NPU_TUNNEL_UDF_SRV6_HEADER_LENGTH_OFFSET 0x49U
#define NPU_TUNNEL_UDF_SRV6_SEGMENTS_LEFT_OFFSET 0x4bU
#define NPU_TUNNEL_UDF_SRV6_FLAGS_OFFSET 0x4dU
#define NPU_TUNNEL_UDF_SRV6_SEGMENT_LIST_OFFSET 0x50U
#define NPU_TUNNEL_UDF_SRV6_ROUTING_NEXT_HEADER 4U
#define NPU_TUNNEL_UDF_SRV6_REMOVE_PREFIX_SIZE 46U
#define NPU_TUNNEL_UDF_ETHERNET_LAYER_2_SIZE 14U
#define NPU_TUNNEL_UDF_MAP_FIRST 65U
#define NPU_TUNNEL_UDF_MAP_LAST 68U
#define NPU_TUNNEL_UDF_MAP_INDEX_OFFSET 16U
#define NPU_TUNNEL_UDF_MAP_METADATA_OFFSET 4U
#define NPU_TUNNEL_UDF_MAP_METADATA_MASK UINT32_C(0x0003ffff)
#define NPU_TUNNEL_UDF_MAP_ETHERTYPE_OFFSET 0x2cU
#define NPU_TUNNEL_UDF_MAP_UDF_65_PREFIX_LENGTH 0x56U
#define NPU_TUNNEL_UDF_MAP_UDF_65_LENGTH_OFFSET 0x32U
#define NPU_TUNNEL_UDF_MAP_UDF_65_PROTOCOL_OFFSET 0x34U
#define NPU_TUNNEL_UDF_MAP_UDF_65_PROTOCOL_SOURCE_OFFSET 0x5fU
#define NPU_TUNNEL_UDF_MAP_UDF_66_PREFIX_LENGTH 0x42U
#define NPU_TUNNEL_UDF_MAP_UDF_66_LENGTH_OFFSET 0x30U
#define NPU_TUNNEL_UDF_MAP_UDF_66_PROTOCOL_OFFSET 0x37U
#define NPU_TUNNEL_UDF_MAP_UDF_66_PROTOCOL_SOURCE_OFFSET 0x48U
#define NPU_TUNNEL_UDF_MAP_UDF_66_METADATA_OFFSET 0x6cU
#define NPU_TUNNEL_UDF_MAP_UDF_66_LENGTH_OVERHEAD 0x56U
#define NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET 0x6aU
#define NPU_TUNNEL_UDF_MAP_PREFIX_LENGTH 0x2eU
#define NPU_TUNNEL_UDF_MAP_IPV6_TAIL_OFFSET 0x42U
#define NPU_TUNNEL_UDF_MAP_IPV6_TEMPLATE_LENGTH 0x28U
#define NPU_TUNNEL_UDF_MAP_IPV6_LENGTH_OFFSET 4U
#define NPU_TUNNEL_UDF_MAP_IPV6_PROTOCOL_OFFSET 6U
#define NPU_TUNNEL_UDF_MAP_IPV6_PROTOCOL_SOURCE_OFFSET 0x37U
#define NPU_TUNNEL_UDF_MAP_IPV4_TAIL_OFFSET 0x5aU
#define NPU_TUNNEL_UDF_MAP_IPV4_TEMPLATE_LENGTH 0x18U
#define NPU_TUNNEL_UDF_MAP_IPV4_LENGTH_OFFSET 2U
#define NPU_TUNNEL_UDF_MAP_IPV4_PROTOCOL_OFFSET 9U
#define NPU_TUNNEL_UDF_MAP_IPV4_PROTOCOL_SOURCE_OFFSET 0x34U

static bool operation_append(struct npu_tunnel_udf_operation_sequence *sequence,
                             uint32_t packet_address, uint32_t length,
                             uint32_t source_offset, uint8_t channel,
                             uint8_t operation_type, bool begin, bool end) {
  struct npu_tunnel_packet_operation *operation;

  if (sequence->operation_count >= NPU_TUNNEL_UDF_OPERATION_COUNT_MAX)
    return false;
  operation = &sequence->operations[sequence->operation_count];
  if (!npu_tunnel_packet_operation_set(operation, packet_address, length,
                                       source_offset, channel, operation_type,
                                       begin, end))
    return false;

  ++sequence->operation_count;
  return true;
}

static bool
map_packet_patch_append(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence,
                        uint32_t offset, const uint8_t *data, size_t length) {
  struct npu_tunnel_udf_map_packet_patch *patch;

  if (data == NULL || length == 0U ||
      length > NPU_TUNNEL_UDF_MAP_PACKET_PATCH_SIZE_MAX ||
      sequence->map_packet_patch_count >=
          NPU_TUNNEL_UDF_MAP_PACKET_PATCH_COUNT_MAX ||
      offset > UINT16_MAX || offset > sequence->received_length ||
      length > (size_t)sequence->received_length - offset ||
      offset > request->packet_extent ||
      length > request->packet_extent - offset)
    return false;

  patch = &sequence->map_packet_patches[sequence->map_packet_patch_count];
  patch->offset = (uint16_t)offset;
  patch->length = (uint8_t)length;
  npu_memcpy(patch->data, data, length);
  ++sequence->map_packet_patch_count;
  return true;
}

static bool
map_packet_patch_u8_append(const struct npu_tunnel_udf_request *request,
                           struct npu_tunnel_udf_operation_sequence *sequence,
                           uint32_t offset, uint8_t value) {
  return map_packet_patch_append(request, sequence, offset, &value,
                                 sizeof(value));
}

static bool
map_packet_patch_u16_append(const struct npu_tunnel_udf_request *request,
                            struct npu_tunnel_udf_operation_sequence *sequence,
                            uint32_t offset, uint16_t value) {
  uint8_t encoded[sizeof(value)] = {
      (uint8_t)(value >> 8),
      (uint8_t)value,
  };

  return map_packet_patch_append(request, sequence, offset, encoded,
                                 sizeof(encoded));
}

static bool
template_address_resolve(const struct npu_tunnel_udf_request *request,
                         uint8_t template_index, uint32_t *template_address) {
  uint32_t offset = (uint32_t)template_index * NPU_TUNNEL_UDF_TEMPLATE_STRIDE;

  if (request->template_base_address == 0U ||
      request->template_base_address > UINT32_MAX - offset)
    return false;
  *template_address = request->template_base_address + offset;
  return true;
}

static bool forward_plan(const struct npu_tunnel_udf_request *request,
                         struct npu_tunnel_udf_operation_sequence *sequence,
                         uint32_t remove_length) {
  if (remove_length == 0U)
    return operation_append(sequence, request->packet_address,
                            sequence->received_length, 0U, request->channel, 0U,
                            true, true);
  if (sequence->received_length <=
      NPU_TUNNEL_PACKET_CONTROL_SIZE + remove_length)
    return false;

  return operation_append(sequence, request->packet_address,
                          NPU_TUNNEL_PACKET_CONTROL_SIZE, 0U, request->channel,
                          1U, true, false) &&
         operation_append(sequence, request->packet_address,
                          sequence->received_length -
                              NPU_TUNNEL_PACKET_CONTROL_SIZE - remove_length,
                          NPU_TUNNEL_PACKET_CONTROL_SIZE + remove_length,
                          request->channel, 0U, false, true);
}

static uint16_t ipv4_checksum_adjust(const uint8_t *ipv4_header,
                                     uint16_t new_total_length,
                                     uint16_t new_fragment_field) {
  uint32_t sum;

  sum = (uint32_t)(~npu_load_big_endian_u16(ipv4_header + 10U) & UINT16_MAX) +
        (uint32_t)(~npu_load_big_endian_u16(ipv4_header + 2U) & UINT16_MAX) +
        new_total_length;
  sum = (sum & UINT16_MAX) + (sum >> 16) +
        (uint32_t)(~npu_load_big_endian_u16(ipv4_header + 6U) & UINT16_MAX) +
        new_fragment_field;
  sum = (sum & UINT16_MAX) + (sum >> 16);
  return (uint16_t)(~sum & UINT16_MAX);
}

static bool
udf_1_to_20_single_plan(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence,
                        uint32_t template_address) {
  struct npu_tunnel_packet_operation *template_operation;

  if ((uint32_t)sequence->received_length + 4U > UINT16_MAX ||
      sequence->received_length < NPU_TUNNEL_PACKET_CONTROL_SIZE)
    return false;
  if (!operation_append(sequence, request->packet_address,
                        NPU_TUNNEL_PACKET_CONTROL_SIZE, 0U, request->channel,
                        1U, true, false) ||
      !operation_append(sequence, template_address,
                        NPU_TUNNEL_UDF_TEMPLATE_HEADER_SIZE, 0U,
                        request->channel, 5U, false, false))
    return false;

  template_operation = &sequence->operations[1];
  template_operation->modifiers[0] = npu_tunnel_bridge_patch_u16(
      0x10U, (uint16_t)(sequence->received_length + 4U));
  template_operation->modifiers[1] = npu_tunnel_bridge_patch_u16(
      0x26U, (uint16_t)(sequence->received_length - 0x10U));
  return operation_append(
      sequence, request->packet_address,
      sequence->received_length - NPU_TUNNEL_PACKET_CONTROL_SIZE,
      NPU_TUNNEL_PACKET_CONTROL_SIZE, request->channel, 0U, false, true);
}

static bool
udf_1_to_20_fragment_plan(const struct npu_tunnel_udf_request *request,
                          struct npu_tunnel_udf_operation_sequence *sequence,
                          uint32_t template_address) {
  struct npu_tunnel_packet_operation *operation;
  const uint8_t *ipv4_header;
  uint32_t first_payload;
  uint32_t fragment_units;
  uint32_t remaining;
  uint32_t second_payload;
  uint32_t tail_length;
  uint32_t tail_source;
  uint16_t checksum;

  if (request->vxlan_mtu < NPU_TUNNEL_UDF_VXLAN_FRAGMENT_MINIMUM)
    return false;
  fragment_units =
      ((uint32_t)request->vxlan_mtu - NPU_TUNNEL_UDF_VXLAN_FRAGMENT_OVERHEAD) /
      8U;
  first_payload = fragment_units * 8U;
  if (first_payload == 0U ||
      first_payload + NPU_TUNNEL_UDF_IPV4_FRAGMENT_SOURCE_OVERHEAD >=
          sequence->received_length)
    return false;

  remaining = sequence->received_length - first_payload;
  if (remaining <= NPU_TUNNEL_UDF_IPV4_FRAGMENT_SOURCE_OVERHEAD)
    return false;
  second_payload = remaining - 0x2eU;
  tail_length = sequence->received_length -
                NPU_TUNNEL_UDF_IPV4_FRAGMENT_SOURCE_OVERHEAD - first_payload;
  tail_source = first_payload + NPU_TUNNEL_UDF_IPV4_FRAGMENT_SOURCE_OVERHEAD;
  if (tail_length == 0U || second_payload > UINT16_MAX ||
      (size_t)NPU_TUNNEL_PACKET_CONTROL_SIZE + sequence->layer_2_offset + 12U >
          request->packet_extent)
    return false;
  ipv4_header = request->packet + NPU_TUNNEL_PACKET_CONTROL_SIZE +
                sequence->layer_2_offset;

  if (!operation_append(sequence, request->packet_address,
                        NPU_TUNNEL_PACKET_CONTROL_SIZE, 0U, request->channel,
                        1U, true, false) ||
      !operation_append(sequence, template_address,
                        NPU_TUNNEL_UDF_TEMPLATE_HEADER_SIZE, 0U,
                        request->channel, 5U, false, false) ||
      !operation_append(sequence, request->packet_address,
                        first_payload +
                            NPU_TUNNEL_UDF_IPV4_FRAGMENT_HEADER_SIZE,
                        NPU_TUNNEL_UDF_IPV4_HEADER_SOURCE_OFFSET,
                        request->channel, 1U, false, true))
    return false;

  operation = &sequence->operations[1];
  operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0x10U, (uint16_t)(first_payload + 0x46U));
  operation->modifiers[1] =
      npu_tunnel_bridge_patch_u16(0x26U, (uint16_t)(first_payload + 0x32U));
  operation = &sequence->operations[2];
  operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0x30U, (uint16_t)(first_payload + 0x14U));
  operation->modifiers[1] =
      npu_tunnel_bridge_patch_u16(0x34U, UINT16_C(0x2000));
  checksum = ipv4_checksum_adjust(
      ipv4_header, (uint16_t)(first_payload + 0x14U), UINT16_C(0x2000));
  operation->modifiers[2] = npu_tunnel_bridge_patch_u16(0x38U, checksum);

  if (!operation_append(sequence, request->packet_address,
                        NPU_TUNNEL_PACKET_CONTROL_SIZE, 0U, request->channel,
                        1U, true, false) ||
      !operation_append(sequence, template_address,
                        NPU_TUNNEL_UDF_TEMPLATE_HEADER_SIZE, 0U,
                        request->channel, 5U, false, false) ||
      !operation_append(sequence, request->packet_address,
                        NPU_TUNNEL_UDF_IPV4_FRAGMENT_HEADER_SIZE,
                        NPU_TUNNEL_UDF_IPV4_HEADER_SOURCE_OFFSET,
                        request->channel, 1U, false, false) ||
      !operation_append(sequence, request->packet_address, tail_length,
                        tail_source, request->channel, 0U, false, true))
    return false;

  operation = &sequence->operations[4];
  operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0x10U, (uint16_t)(remaining + 4U));
  operation->modifiers[1] =
      npu_tunnel_bridge_patch_u16(0x26U, (uint16_t)(remaining - 0x10U));
  operation = &sequence->operations[5];
  operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0x30U, (uint16_t)second_payload);
  operation->modifiers[1] =
      npu_tunnel_bridge_patch_u16(0x34U, (uint16_t)fragment_units);
  checksum = ipv4_checksum_adjust(ipv4_header, (uint16_t)second_payload,
                                  (uint16_t)fragment_units);
  operation->modifiers[2] = npu_tunnel_bridge_patch_u16(0x38U, checksum);
  return true;
}

static bool
udf_1_to_20_plan(const struct npu_tunnel_udf_request *request,
                 struct npu_tunnel_udf_operation_sequence *sequence) {
  uint32_t template_address;

  if (!template_address_resolve(request, (uint8_t)(sequence->udf - 1U),
                                &template_address))
    return false;
  if (request->vxlan_mtu >= (uint32_t)sequence->received_length + 4U)
    return udf_1_to_20_single_plan(request, sequence, template_address);
  return udf_1_to_20_fragment_plan(request, sequence, template_address);
}

static bool
udf_41_to_48_plan(const struct npu_tunnel_udf_request *request,
                  struct npu_tunnel_udf_operation_sequence *sequence) {
  struct npu_tunnel_packet_operation *template_operation;
  uint32_t patch_value;
  uint32_t template_address;
  uint8_t header_index =
      (uint8_t)(sequence->udf - NPU_TUNNEL_UDF_SRV6_TEMPLATE_FIRST);
  uint8_t header_length = request->srv6_header_lengths[header_index];

  if (header_length <= 12U ||
      !template_address_resolve(request, (uint8_t)(sequence->udf - 21U),
                                &template_address) ||
      sequence->received_length <= NPU_TUNNEL_UDF_SRV6_REMOVE_PREFIX_SIZE ||
      (uint32_t)sequence->received_length + header_length <
          (uint32_t)sequence->layer_2_offset + 0x56U)
    return false;
  patch_value = (uint32_t)sequence->received_length - sequence->layer_2_offset -
                0x56U + header_length;
  if (patch_value > UINT16_MAX)
    return false;

  if (!operation_append(sequence, request->packet_address, 0x2cU, 0U,
                        request->channel, 1U, true, false) ||
      !operation_append(sequence, template_address,
                        (uint32_t)header_length - 12U, 12U, request->channel,
                        5U, false, false) ||
      !operation_append(sequence, request->packet_address,
                        sequence->received_length -
                            NPU_TUNNEL_UDF_SRV6_REMOVE_PREFIX_SIZE,
                        NPU_TUNNEL_UDF_SRV6_REMOVE_PREFIX_SIZE,
                        request->channel, 0U, false, true))
    return false;

  template_operation = &sequence->operations[1];
  template_operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0x12U, (uint16_t)patch_value);
  return true;
}

static bool
udf_49_to_56_strip_plan(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence,
                        uint8_t header_length, bool remove_srv6_header) {
  struct npu_tunnel_packet_operation *prefix_operation;
  uint32_t source_offset = 0x56U;

  if (remove_srv6_header)
    source_offset = (uint32_t)header_length * 8U + 0x5eU;

  if (source_offset >= sequence->received_length ||
      !operation_append(sequence, request->packet_address,
                        NPU_TUNNEL_UDF_SRV6_REMOVE_PREFIX_SIZE, 0U,
                        request->channel, 1U, true, false) ||
      !operation_append(sequence, request->packet_address,
                        sequence->received_length - source_offset,
                        source_offset, request->channel, 0U, false, true))
    return false;
  prefix_operation = &sequence->operations[0];
  prefix_operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0x2cU, UINT16_C(0x0800));
  return true;
}

static bool
udf_49_to_56_remove_plan(const struct npu_tunnel_udf_request *request,
                         struct npu_tunnel_udf_operation_sequence *sequence,
                         uint8_t header_length) {
  uint32_t header_size = (uint32_t)header_length * 8U + 8U;
  uint32_t prefix_length =
      (uint32_t)sequence->layer_2_offset + NPU_TUNNEL_UDF_SRV6_HEADER_OFFSET;
  uint32_t source_offset = prefix_length + header_size;
  uint16_t payload_length = npu_load_big_endian_u16(
      request->packet + sequence->ipv6_payload_length_offset);

  if (payload_length < header_size || source_offset > sequence->received_length)
    return false;
  sequence->ipv6_payload_length = (uint16_t)(payload_length - header_size);
  if ((uint32_t)sequence->received_length - source_offset !=
      sequence->ipv6_payload_length)
    return false;

  return operation_append(sequence, request->packet_address, prefix_length, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->packet_address,
                          sequence->ipv6_payload_length, source_offset,
                          request->channel, 0U, false, true);
}

static bool
udf_49_to_56_plan(const struct npu_tunnel_udf_request *request,
                  struct npu_tunnel_udf_operation_sequence *sequence) {
  uint32_t segment_offset;
  uint8_t flags;
  uint8_t header_length;
  uint8_t next_header;
  uint8_t segments_left;

  if (sequence->layer_2_offset != NPU_TUNNEL_UDF_ETHERNET_LAYER_2_SIZE)
    return false;
  sequence->ipv6_next_header_offset =
      (uint16_t)(sequence->layer_2_offset +
                 NPU_TUNNEL_UDF_SRV6_NEXT_HEADER_OFFSET);
  sequence->ipv6_payload_length_offset =
      (uint16_t)(sequence->layer_2_offset +
                 NPU_TUNNEL_UDF_IPV6_PAYLOAD_LENGTH_OFFSET);
  sequence->srv6_destination_offset =
      (uint16_t)(sequence->layer_2_offset +
                 NPU_TUNNEL_UDF_IPV6_DESTINATION_OFFSET);
  sequence->srv6_segments_left_offset =
      (uint16_t)(sequence->layer_2_offset +
                 NPU_TUNNEL_UDF_SRV6_SEGMENTS_LEFT_OFFSET);
  if ((uint32_t)sequence->layer_2_offset + NPU_TUNNEL_UDF_SRV6_FLAGS_OFFSET >=
          sequence->received_length ||
      (size_t)sequence->layer_2_offset + NPU_TUNNEL_UDF_SRV6_FLAGS_OFFSET >=
          request->packet_extent)
    return false;

  next_header = request->packet[sequence->ipv6_next_header_offset];
  header_length = request->packet[sequence->layer_2_offset +
                                  NPU_TUNNEL_UDF_SRV6_HEADER_LENGTH_OFFSET];
  segments_left = request->packet[sequence->srv6_segments_left_offset];
  if (next_header == NPU_TUNNEL_UDF_SRV6_ROUTING_NEXT_HEADER)
    return udf_49_to_56_strip_plan(request, sequence, header_length, false);
  if (segments_left == 0U)
    return udf_49_to_56_strip_plan(request, sequence, header_length, true);

  sequence->srv6_segments_left = (uint8_t)(segments_left - 1U);
  segment_offset =
      (uint32_t)sequence->layer_2_offset +
      NPU_TUNNEL_UDF_SRV6_SEGMENT_LIST_OFFSET +
      (uint32_t)sequence->srv6_segments_left * NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE;
  if (segment_offset > UINT16_MAX ||
      segment_offset + NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE >
          sequence->received_length ||
      segment_offset + NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE >
          request->packet_extent ||
      (size_t)sequence->srv6_destination_offset +
              NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE >
          request->packet_extent)
    return false;
  sequence->srv6_segment_offset = (uint16_t)segment_offset;
  sequence->srv6_segment_advance = true;

  flags =
      request
          ->packet[sequence->layer_2_offset + NPU_TUNNEL_UDF_SRV6_FLAGS_OFFSET];
  if (sequence->srv6_segments_left == 0U && (flags & 0x80U) != 0U) {
    sequence->srv6_header_remove = true;
    sequence->ipv6_next_header =
        request->packet[sequence->layer_2_offset +
                        NPU_TUNNEL_UDF_SRV6_HEADER_OFFSET];
    return udf_49_to_56_remove_plan(request, sequence, header_length);
  }
  sequence->srv6_segments_left_write = true;
  return forward_plan(request, sequence, 0U);
}

static bool udf_65_plan(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence) {
  if (sequence->received_length <= NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET ||
      !map_packet_patch_u16_append(
          request, sequence, NPU_TUNNEL_UDF_MAP_UDF_65_LENGTH_OFFSET,
          (uint16_t)(sequence->received_length -
                     NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET)) ||
      !map_packet_patch_u8_append(
          request, sequence, NPU_TUNNEL_UDF_MAP_UDF_65_PROTOCOL_OFFSET,
          request->packet[NPU_TUNNEL_UDF_MAP_UDF_65_PROTOCOL_SOURCE_OFFSET]))
    return false;

  return operation_append(sequence, request->packet_address,
                          NPU_TUNNEL_UDF_MAP_UDF_65_PREFIX_LENGTH, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->packet_address,
                          sequence->received_length -
                              NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET,
                          NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET,
                          request->channel, 0U, false, true);
}

static bool udf_66_plan(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence) {
  uint32_t map_metadata;

  if (sequence->received_length <=
      NPU_TUNNEL_UDF_MAP_UDF_66_METADATA_OFFSET + sizeof(uint16_t) - 1U)
    return false;
  map_metadata = npu_load_little_endian_u32(
                     request->packet + NPU_TUNNEL_UDF_MAP_METADATA_OFFSET) &
                 NPU_TUNNEL_UDF_MAP_METADATA_MASK;
  if (!map_packet_patch_u16_append(
          request, sequence, NPU_TUNNEL_UDF_MAP_UDF_66_LENGTH_OFFSET,
          (uint16_t)(sequence->received_length -
                     NPU_TUNNEL_UDF_MAP_UDF_66_LENGTH_OVERHEAD)) ||
      !map_packet_patch_u8_append(
          request, sequence, NPU_TUNNEL_UDF_MAP_UDF_66_PROTOCOL_OFFSET,
          request->packet[NPU_TUNNEL_UDF_MAP_UDF_66_PROTOCOL_SOURCE_OFFSET]) ||
      !map_packet_patch_u16_append(request, sequence,
                                   NPU_TUNNEL_UDF_MAP_UDF_66_METADATA_OFFSET,
                                   (uint16_t)map_metadata))
    return false;

  return operation_append(sequence, request->packet_address,
                          NPU_TUNNEL_UDF_MAP_UDF_66_PREFIX_LENGTH, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->packet_address,
                          sequence->received_length -
                              NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET,
                          NPU_TUNNEL_UDF_MAP_REMOVE_TAIL_OFFSET,
                          request->channel, 0U, false, true);
}

static bool
map_template_copy(const struct npu_tunnel_udf_request *request,
                  struct npu_tunnel_udf_operation_sequence *sequence,
                  uint8_t length) {
  uint16_t map_index;
  size_t record_offset;

  if (length == 0U || length > NPU_TUNNEL_UDF_MAP_RECORD_SIZE ||
      request->map_info_records == NULL ||
      request->map_template_scratch == NULL ||
      request->map_template_scratch == request->packet ||
      request->map_template_scratch_address == 0U ||
      request->map_template_scratch_extent < length ||
      sequence->received_length <
          NPU_TUNNEL_UDF_MAP_INDEX_OFFSET + sizeof(uint16_t) ||
      request->packet_extent <
          NPU_TUNNEL_UDF_MAP_INDEX_OFFSET + sizeof(uint16_t))
    return false;

  map_index = npu_load_little_endian_u16(request->packet +
                                         NPU_TUNNEL_UDF_MAP_INDEX_OFFSET);
  record_offset = (size_t)map_index * NPU_TUNNEL_UDF_MAP_RECORD_SIZE;
  if (record_offset > request->map_info_extent ||
      NPU_TUNNEL_UDF_MAP_RECORD_SIZE > request->map_info_extent - record_offset)
    return false;

  npu_memcpy(sequence->map_template, request->map_info_records + record_offset,
             length);
  sequence->map_template_length = length;
  return true;
}

static bool udf_67_plan(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence) {
  uint16_t payload_length;

  if (sequence->received_length <= NPU_TUNNEL_UDF_MAP_IPV6_TAIL_OFFSET ||
      !map_template_copy(request, sequence,
                         NPU_TUNNEL_UDF_MAP_IPV6_TEMPLATE_LENGTH))
    return false;
  payload_length = (uint16_t)(sequence->received_length -
                              NPU_TUNNEL_UDF_MAP_IPV6_TAIL_OFFSET);
  npu_store_big_endian_u16(sequence->map_template +
                               NPU_TUNNEL_UDF_MAP_IPV6_LENGTH_OFFSET,
                           payload_length);
  sequence->map_template[NPU_TUNNEL_UDF_MAP_IPV6_PROTOCOL_OFFSET] =
      request->packet[NPU_TUNNEL_UDF_MAP_IPV6_PROTOCOL_SOURCE_OFFSET];
  if (!map_packet_patch_u16_append(request, sequence,
                                   NPU_TUNNEL_UDF_MAP_ETHERTYPE_OFFSET,
                                   UINT16_C(0x86dd)))
    return false;

  return operation_append(sequence, request->packet_address,
                          NPU_TUNNEL_UDF_MAP_PREFIX_LENGTH, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->map_template_scratch_address,
                          NPU_TUNNEL_UDF_MAP_IPV6_TEMPLATE_LENGTH, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->packet_address, payload_length,
                          NPU_TUNNEL_UDF_MAP_IPV6_TAIL_OFFSET, request->channel,
                          0U, false, true);
}

static bool udf_68_plan(const struct npu_tunnel_udf_request *request,
                        struct npu_tunnel_udf_operation_sequence *sequence) {
  uint16_t ipv4_total_length;

  if (sequence->received_length <= NPU_TUNNEL_UDF_MAP_IPV4_TAIL_OFFSET ||
      !map_template_copy(request, sequence,
                         NPU_TUNNEL_UDF_MAP_IPV4_TEMPLATE_LENGTH))
    return false;
  ipv4_total_length = (uint16_t)(sequence->received_length -
                                 NPU_TUNNEL_UDF_MAP_IPV6_TAIL_OFFSET);
  npu_store_big_endian_u16(sequence->map_template +
                               NPU_TUNNEL_UDF_MAP_IPV4_LENGTH_OFFSET,
                           ipv4_total_length);
  sequence->map_template[NPU_TUNNEL_UDF_MAP_IPV4_PROTOCOL_OFFSET] =
      request->packet[NPU_TUNNEL_UDF_MAP_IPV4_PROTOCOL_SOURCE_OFFSET];
  if (!map_packet_patch_u16_append(request, sequence,
                                   NPU_TUNNEL_UDF_MAP_ETHERTYPE_OFFSET,
                                   UINT16_C(0x0800)))
    return false;

  return operation_append(sequence, request->packet_address,
                          NPU_TUNNEL_UDF_MAP_PREFIX_LENGTH, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->map_template_scratch_address,
                          NPU_TUNNEL_UDF_MAP_IPV4_TEMPLATE_LENGTH, 0U,
                          request->channel, 1U, true, false) &&
         operation_append(sequence, request->packet_address,
                          sequence->received_length -
                              NPU_TUNNEL_UDF_MAP_IPV4_TAIL_OFFSET,
                          NPU_TUNNEL_UDF_MAP_IPV4_TAIL_OFFSET, request->channel,
                          0U, false, true);
}

static bool
udf_65_to_68_plan(const struct npu_tunnel_udf_request *request,
                  struct npu_tunnel_udf_operation_sequence *sequence) {
  if (sequence->layer_2_offset != NPU_TUNNEL_UDF_ETHERNET_LAYER_2_SIZE)
    return false;
  if (sequence->udf == 65U)
    return udf_65_plan(request, sequence);
  if (sequence->udf == 66U)
    return udf_66_plan(request, sequence);
  if (sequence->udf == 67U)
    return udf_67_plan(request, sequence);
  return udf_68_plan(request, sequence);
}

bool npu_tunnel_udf_operations_plan(
    const struct npu_tunnel_udf_request *request,
    struct npu_tunnel_udf_operation_sequence *sequence) {
  struct npu_tunnel_udf_operation_sequence decoded = {0};
  uint32_t metadata;
  uint32_t wire_length;
  bool planned;

  if (request == NULL || sequence == NULL || request->packet == NULL ||
      request->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      request->packet_extent < NPU_TUNNEL_PACKET_CONTROL_SIZE)
    return false;

  metadata = npu_load_little_endian_u32(request->packet);
  wire_length = metadata & UINT16_MAX;
  if (wire_length > UINT16_MAX - NPU_TUNNEL_PACKET_CONTROL_SIZE ||
      (size_t)NPU_TUNNEL_PACKET_CONTROL_SIZE + wire_length >
          request->packet_extent)
    return false;
  decoded.received_length =
      (uint16_t)(wire_length + NPU_TUNNEL_PACKET_CONTROL_SIZE);
  decoded.layer_2_offset =
      (uint8_t)((metadata >> NPU_TUNNEL_UDF_LAYER_2_OFFSET_SHIFT) &
                NPU_TUNNEL_UDF_LAYER_2_OFFSET_MASK);
  decoded.udf = request->packet[NPU_TUNNEL_PACKET_UDF_OFFSET];

  if (decoded.udf >= 1U && decoded.udf <= 20U)
    planned = udf_1_to_20_plan(request, &decoded);
  else if (decoded.udf >= 21U && decoded.udf <= 40U)
    planned =
        forward_plan(request, &decoded, NPU_TUNNEL_UDF_FORWARD_REMOVE_SIZE);
  else if (decoded.udf >= NPU_TUNNEL_UDF_SRV6_TEMPLATE_FIRST &&
           decoded.udf <= NPU_TUNNEL_UDF_SRV6_TEMPLATE_LAST)
    planned = udf_41_to_48_plan(request, &decoded);
  else if (decoded.udf >= NPU_TUNNEL_UDF_SRV6_ADVANCE_FIRST &&
           decoded.udf <= NPU_TUNNEL_UDF_SRV6_ADVANCE_LAST)
    planned = udf_49_to_56_plan(request, &decoded);
  else if (decoded.udf >= NPU_TUNNEL_UDF_MAP_FIRST &&
           decoded.udf <= NPU_TUNNEL_UDF_MAP_LAST)
    planned = udf_65_to_68_plan(request, &decoded);
  else
    return false;
  if (!planned)
    return false;

  *sequence = decoded;
  return true;
}

static bool
udf_commands_encode(const struct npu_tunnel_udf_operation_sequence *sequence,
                    struct npu_tunnel_bridge_command *commands) {
  size_t operation_index;

  for (operation_index = 0U; operation_index < sequence->operation_count;
       ++operation_index) {
    if (!npu_tunnel_packet_operation_encode(
            &sequence->operations[operation_index], &commands[operation_index]))
      return false;
  }
  return true;
}

static void udf_packet_control_write(
    const struct npu_tunnel_udf_request *request,
    const struct npu_tunnel_udf_operation_sequence *sequence) {
  npu_store_little_endian_u32(
      request->packet, (uint32_t)request->channel
                           << NPU_TUNNEL_UDF_PACKET_CONTROL_CHANNEL_SHIFT);
  npu_store_little_endian_u32(
      request->packet + NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_4_OFFSET,
      (uint32_t)sequence->udf << 14 | NPU_TUNNEL_UDF_PACKET_CONTROL_ROUTE);
  npu_store_little_endian_u32(request->packet +
                                  NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_5_OFFSET,
                              NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_5);
  npu_store_little_endian_u32(request->packet +
                                  NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_6_OFFSET,
                              NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_6);
}

enum npu_runtime_result
npu_tunnel_udf_execute(const struct npu_tunnel_udf_request *request,
                       const struct npu_tunnel_bridge_backend *bridge) {
  struct npu_tunnel_bridge_command commands[NPU_TUNNEL_UDF_OPERATION_COUNT_MAX];
  struct npu_tunnel_udf_operation_sequence sequence;
  enum npu_runtime_result result;
  uint32_t original_control_words[4];
  uint8_t original_destination[NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE];
  uint8_t original_map_packet_patches[NPU_TUNNEL_UDF_MAP_PACKET_PATCH_COUNT_MAX]
                                     [NPU_TUNNEL_UDF_MAP_PACKET_PATCH_SIZE_MAX];
  uint8_t original_map_template[NPU_TUNNEL_UDF_MAP_RECORD_SIZE];
  uint8_t original_next_header = 0U;
  uint8_t original_payload_length[2] = {0};
  uint8_t original_segments_left = 0U;
  size_t patch_index;

  if (request == NULL || bridge == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!npu_tunnel_udf_operations_plan(request, &sequence) ||
      !udf_commands_encode(&sequence, commands))
    return NPU_RUNTIME_REJECTED;

  original_control_words[0] = npu_load_little_endian_u32(request->packet);
  original_control_words[1] = npu_load_little_endian_u32(
      request->packet + NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_4_OFFSET);
  original_control_words[2] = npu_load_little_endian_u32(
      request->packet + NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_5_OFFSET);
  original_control_words[3] = npu_load_little_endian_u32(
      request->packet + NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_6_OFFSET);
  if (sequence.srv6_segment_advance) {
    npu_memcpy(original_destination,
               request->packet + sequence.srv6_destination_offset,
               sizeof(original_destination));
    if (sequence.srv6_segments_left_write)
      original_segments_left =
          request->packet[sequence.srv6_segments_left_offset];
  }
  if (sequence.srv6_header_remove) {
    original_next_header = request->packet[sequence.ipv6_next_header_offset];
    original_payload_length[0] =
        request->packet[sequence.ipv6_payload_length_offset];
    original_payload_length[1] =
        request->packet[sequence.ipv6_payload_length_offset + 1U];
  }
  for (patch_index = 0U; patch_index < sequence.map_packet_patch_count;
       ++patch_index) {
    const struct npu_tunnel_udf_map_packet_patch *patch =
        &sequence.map_packet_patches[patch_index];

    npu_memcpy(original_map_packet_patches[patch_index],
               request->packet + patch->offset, patch->length);
  }
  if (sequence.map_template_length != 0U)
    npu_memcpy(original_map_template, request->map_template_scratch,
               sequence.map_template_length);

  udf_packet_control_write(request, &sequence);
  if (sequence.srv6_segment_advance) {
    npu_memmove(request->packet + sequence.srv6_destination_offset,
                request->packet + sequence.srv6_segment_offset,
                NPU_TUNNEL_UDF_SRV6_SEGMENT_SIZE);
    if (sequence.srv6_segments_left_write)
      request->packet[sequence.srv6_segments_left_offset] =
          sequence.srv6_segments_left;
  }
  if (sequence.srv6_header_remove) {
    request->packet[sequence.ipv6_next_header_offset] =
        sequence.ipv6_next_header;
    npu_store_big_endian_u16(request->packet +
                                 sequence.ipv6_payload_length_offset,
                             sequence.ipv6_payload_length);
  }
  for (patch_index = 0U; patch_index < sequence.map_packet_patch_count;
       ++patch_index) {
    const struct npu_tunnel_udf_map_packet_patch *patch =
        &sequence.map_packet_patches[patch_index];

    npu_memcpy(request->packet + patch->offset, patch->data, patch->length);
  }
  if (sequence.map_template_length != 0U)
    npu_memcpy(request->map_template_scratch, sequence.map_template,
               sequence.map_template_length);

  result = npu_tunnel_bridge_commands_publish(
      bridge, request->channel, commands, sequence.operation_count);
  if (result == NPU_RUNTIME_SUCCESS)
    return result;

  npu_store_little_endian_u32(request->packet, original_control_words[0]);
  npu_store_little_endian_u32(request->packet +
                                  NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_4_OFFSET,
                              original_control_words[1]);
  npu_store_little_endian_u32(request->packet +
                                  NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_5_OFFSET,
                              original_control_words[2]);
  npu_store_little_endian_u32(request->packet +
                                  NPU_TUNNEL_UDF_PACKET_CONTROL_WORD_6_OFFSET,
                              original_control_words[3]);
  if (sequence.srv6_segment_advance) {
    npu_memcpy(request->packet + sequence.srv6_destination_offset,
               original_destination, sizeof(original_destination));
    if (sequence.srv6_segments_left_write)
      request->packet[sequence.srv6_segments_left_offset] =
          original_segments_left;
  }
  if (sequence.srv6_header_remove) {
    request->packet[sequence.ipv6_next_header_offset] = original_next_header;
    request->packet[sequence.ipv6_payload_length_offset] =
        original_payload_length[0];
    request->packet[sequence.ipv6_payload_length_offset + 1U] =
        original_payload_length[1];
  }
  for (patch_index = 0U; patch_index < sequence.map_packet_patch_count;
       ++patch_index) {
    const struct npu_tunnel_udf_map_packet_patch *patch =
        &sequence.map_packet_patches[patch_index];

    npu_memcpy(request->packet + patch->offset,
               original_map_packet_patches[patch_index], patch->length);
  }
  if (sequence.map_template_length != 0U)
    npu_memcpy(request->map_template_scratch, original_map_template,
               sequence.map_template_length);
  return result;
}
