/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/reassembly.h"

#include "an7581/runtime/endian.h"

#define NPU_TUNNEL_REASSEMBLY_LAYER_2_OFFSET_SHIFT 20U
#define NPU_TUNNEL_REASSEMBLY_LAYER_2_OFFSET_MASK UINT32_C(0x7f)
#define NPU_TUNNEL_REASSEMBLY_IPV4_FLAG UINT32_C(1) << 27
#define NPU_TUNNEL_REASSEMBLY_IPV6_FLAG UINT32_C(1) << 28
#define NPU_TUNNEL_REASSEMBLY_PACKET_CHANNEL_SHIFT 4U
#define NPU_TUNNEL_REASSEMBLY_IPV4_CONTROL UINT32_C(0x00002200)
#define NPU_TUNNEL_REASSEMBLY_IPV6_CONTROL UINT32_C(0x00000200)
#define NPU_TUNNEL_REASSEMBLY_IPV4_HEADER_OFFSET 0x20U
#define NPU_TUNNEL_REASSEMBLY_IPV4_TOTAL_LENGTH_OFFSET 0x22U
#define NPU_TUNNEL_REASSEMBLY_IPV4_IDENTIFICATION_OFFSET 0x24U
#define NPU_TUNNEL_REASSEMBLY_IPV4_FRAGMENT_FIELD_OFFSET 0x26U
#define NPU_TUNNEL_REASSEMBLY_IPV4_HEADER_SIZE 20U
#define NPU_TUNNEL_REASSEMBLY_IPV4_PAYLOAD_OFFSET 0x34U
#define NPU_TUNNEL_REASSEMBLY_IPV4_MORE_FRAGMENTS UINT16_C(0x2000)
#define NPU_TUNNEL_REASSEMBLY_IPV4_OFFSET_MASK UINT16_C(0x1fff)
#define NPU_TUNNEL_REASSEMBLY_IPV6_PAYLOAD_LENGTH_OFFSET 0x24U
#define NPU_TUNNEL_REASSEMBLY_IPV6_NEXT_HEADER_OFFSET 0x26U
#define NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_NEXT_HEADER_OFFSET 0x48U
#define NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_FIELD_OFFSET 0x4aU
#define NPU_TUNNEL_REASSEMBLY_IPV6_IDENTIFICATION_OFFSET 0x4cU
#define NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET 0x50U
#define NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PREFIX_SIZE 0x48U
#define NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_HEADER_SIZE 8U
#define NPU_TUNNEL_REASSEMBLY_IPV6_MORE_FRAGMENTS UINT16_C(1)
#define NPU_TUNNEL_REASSEMBLY_IPV6_OFFSET_MASK UINT16_C(0xfff8)
#define NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_NEXT_HEADER 44U
#define NPU_TUNNEL_REASSEMBLY_PPPOE_ETHERTYPE UINT16_C(0x8864)
#define NPU_TUNNEL_REASSEMBLY_VLAN_ETHERTYPE UINT16_C(0x8100)
#define NPU_TUNNEL_REASSEMBLY_ETHERTYPE_OFFSET 44U
#define NPU_TUNNEL_REASSEMBLY_VLAN_INNER_ETHERTYPE_OFFSET 48U
#define NPU_TUNNEL_REASSEMBLY_PPPOE_LENGTH_OFFSET_DIRECT 0x32U
#define NPU_TUNNEL_REASSEMBLY_PPPOE_LENGTH_OFFSET_VLAN 0x36U
#define NPU_TUNNEL_REASSEMBLY_IPV4_PPPOE_OVERHEAD 0x52U
#define NPU_TUNNEL_REASSEMBLY_IPV6_PPPOE_OVERHEAD 0x76U

struct decoded_packet {
  uint16_t received_length;
  uint8_t layer_2_offset;
  enum npu_tunnel_ip_version ip_version;
};

static bool packet_decode(const struct npu_tunnel_reassembly_packet *packet,
                          struct decoded_packet *decoded) {
  uint32_t metadata;
  uint32_t received_length;
  bool ipv4;
  bool ipv6;

  if (packet == NULL || decoded == NULL || packet->packet == NULL ||
      packet->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      packet->packet_extent < NPU_TUNNEL_PACKET_CONTROL_SIZE)
    return false;

  metadata = npu_load_little_endian_u32(packet->packet);
  received_length = (metadata & UINT16_MAX) + NPU_TUNNEL_PACKET_CONTROL_SIZE;
  ipv4 = (metadata & NPU_TUNNEL_REASSEMBLY_IPV4_FLAG) != 0U;
  ipv6 = (metadata & NPU_TUNNEL_REASSEMBLY_IPV6_FLAG) != 0U;
  if (received_length > UINT16_MAX || received_length > packet->packet_extent ||
      ipv4 == ipv6)
    return false;

  *decoded = (struct decoded_packet){
      .received_length = (uint16_t)received_length,
      .layer_2_offset =
          (uint8_t)((metadata >> NPU_TUNNEL_REASSEMBLY_LAYER_2_OFFSET_SHIFT) &
                    NPU_TUNNEL_REASSEMBLY_LAYER_2_OFFSET_MASK),
      .ip_version = ipv4 ? NPU_TUNNEL_IP_VERSION_4 : NPU_TUNNEL_IP_VERSION_6,
  };
  return true;
}

static bool pending_valid(const struct npu_tunnel_reassembly_pending *pending) {
  return pending->active && pending->packet != NULL &&
         pending->received_length <= pending->packet_extent &&
         pending->channel < NPU_TUNNEL_BRIDGE_CHANNEL_COUNT;
}

static uint8_t pppoe_length_offset(const uint8_t *packet,
                                   uint16_t received_length) {
  if (received_length >=
          NPU_TUNNEL_REASSEMBLY_ETHERTYPE_OFFSET + sizeof(uint16_t) &&
      npu_load_big_endian_u16(packet +
                              NPU_TUNNEL_REASSEMBLY_ETHERTYPE_OFFSET) ==
          NPU_TUNNEL_REASSEMBLY_PPPOE_ETHERTYPE)
    return NPU_TUNNEL_REASSEMBLY_PPPOE_LENGTH_OFFSET_DIRECT;
  if (received_length >= NPU_TUNNEL_REASSEMBLY_VLAN_INNER_ETHERTYPE_OFFSET +
                             sizeof(uint16_t) &&
      npu_load_big_endian_u16(packet +
                              NPU_TUNNEL_REASSEMBLY_ETHERTYPE_OFFSET) ==
          NPU_TUNNEL_REASSEMBLY_VLAN_ETHERTYPE &&
      npu_load_big_endian_u16(
          packet + NPU_TUNNEL_REASSEMBLY_VLAN_INNER_ETHERTYPE_OFFSET) ==
          NPU_TUNNEL_REASSEMBLY_PPPOE_ETHERTYPE)
    return NPU_TUNNEL_REASSEMBLY_PPPOE_LENGTH_OFFSET_VLAN;
  return 0U;
}

static bool pppoe_patch_set(const struct npu_tunnel_reassembly_pending *pending,
                            const struct decoded_packet *current,
                            const uint8_t *current_packet, uint32_t overhead,
                            struct npu_tunnel_packet_operation *operation) {
  uint8_t offset =
      pppoe_length_offset(current_packet, current->received_length);
  uint32_t length;

  if (offset == 0U)
    return true;
  if ((uint32_t)pending->received_length < overhead + pending->layer_2_offset)
    return false;
  length = (uint32_t)pending->received_length - overhead -
           pending->layer_2_offset + current->received_length -
           current->layer_2_offset;
  if (length > UINT16_MAX)
    return false;

  operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(offset, (uint16_t)length);
  return true;
}

static bool
operation_append(struct npu_tunnel_reassembly_operation_sequence *sequence,
                 uint32_t packet_address, uint32_t length,
                 uint32_t source_offset, uint8_t channel, uint8_t operation,
                 bool begin, bool end) {
  if (sequence->operation_count >= NPU_TUNNEL_REASSEMBLY_OPERATION_COUNT_MAX)
    return false;
  if (!npu_tunnel_packet_operation_set(
          &sequence->operations[sequence->operation_count], packet_address,
          length, source_offset, channel, operation, begin, end))
    return false;

  ++sequence->operation_count;
  return true;
}

static bool
ipv4_plan(const struct npu_tunnel_reassembly_runtime *runtime,
          const struct npu_tunnel_reassembly_packet *packet,
          const struct decoded_packet *decoded,
          struct npu_tunnel_reassembly_operation_sequence *sequence) {
  const struct npu_tunnel_reassembly_pending *pending = &runtime->ipv4;
  uint32_t fragment_field_offset =
      (uint32_t)decoded->layer_2_offset +
      NPU_TUNNEL_REASSEMBLY_IPV4_FRAGMENT_FIELD_OFFSET;
  uint32_t header_offset = (uint32_t)decoded->layer_2_offset +
                           NPU_TUNNEL_REASSEMBLY_IPV4_HEADER_OFFSET;
  uint16_t fragment_field;
  uint16_t identification;
  uint16_t pending_identification;
  uint16_t pending_total_length;
  uint16_t current_payload_length;
  uint16_t header_length;
  uint16_t fragment_offset;
  uint32_t new_total_length;

  if (fragment_field_offset + sizeof(uint16_t) > decoded->received_length)
    return false;
  fragment_field =
      npu_load_big_endian_u16(packet->packet + fragment_field_offset);
  sequence->packet_control = NPU_TUNNEL_REASSEMBLY_IPV4_CONTROL |
                             (uint32_t)packet->channel
                                 << NPU_TUNNEL_REASSEMBLY_PACKET_CHANNEL_SHIFT;
  if ((fragment_field & NPU_TUNNEL_REASSEMBLY_IPV4_MORE_FRAGMENTS) != 0U) {
    if (header_offset >= decoded->received_length)
      return false;
    if (pending->active &&
        (!pending_valid(pending) || pending->packet == packet->packet))
      return false;
    header_length = (uint16_t)(packet->packet[header_offset] & 0x0fU) * 4U;
    if (header_length < NPU_TUNNEL_REASSEMBLY_IPV4_HEADER_SIZE ||
        header_offset + header_length > decoded->received_length)
      return false;
    sequence->action = NPU_TUNNEL_REASSEMBLY_STORE_IPV4;
    sequence->current_fragment_field_offset = (uint16_t)fragment_field_offset;
    sequence->current_fragment_field =
        (uint16_t)(fragment_field &
                   (uint16_t)~NPU_TUNNEL_REASSEMBLY_IPV4_MORE_FRAGMENTS);
    sequence->current_fragment_field_write = true;
    sequence->replaces_pending = pending->active;
    return true;
  }

  if (!pending_valid(pending) || pending->channel != packet->channel ||
      pending->packet == packet->packet)
    return false;
  if ((size_t)pending->layer_2_offset +
          NPU_TUNNEL_REASSEMBLY_IPV4_FRAGMENT_FIELD_OFFSET + sizeof(uint16_t) >
      pending->received_length)
    return false;
  if (decoded->received_length <= (uint32_t)decoded->layer_2_offset +
                                      NPU_TUNNEL_REASSEMBLY_IPV4_PAYLOAD_OFFSET)
    return false;

  identification =
      npu_load_big_endian_u16(packet->packet + decoded->layer_2_offset +
                              NPU_TUNNEL_REASSEMBLY_IPV4_IDENTIFICATION_OFFSET);
  pending_identification =
      npu_load_big_endian_u16(pending->packet + pending->layer_2_offset +
                              NPU_TUNNEL_REASSEMBLY_IPV4_IDENTIFICATION_OFFSET);
  if (identification != pending_identification)
    return false;

  pending_total_length =
      npu_load_big_endian_u16(pending->packet + pending->layer_2_offset +
                              NPU_TUNNEL_REASSEMBLY_IPV4_TOTAL_LENGTH_OFFSET);
  header_offset = (uint32_t)pending->layer_2_offset +
                  NPU_TUNNEL_REASSEMBLY_IPV4_HEADER_OFFSET;
  header_length = (uint16_t)(pending->packet[header_offset] & 0x0fU) * 4U;
  fragment_offset =
      (uint16_t)(fragment_field & NPU_TUNNEL_REASSEMBLY_IPV4_OFFSET_MASK) * 8U;
  if (header_length < NPU_TUNNEL_REASSEMBLY_IPV4_HEADER_SIZE ||
      pending_total_length < header_length ||
      fragment_offset != pending_total_length - header_length)
    return false;

  current_payload_length =
      (uint16_t)(decoded->received_length - decoded->layer_2_offset -
                 NPU_TUNNEL_REASSEMBLY_IPV4_PAYLOAD_OFFSET);
  new_total_length = pending_total_length + current_payload_length;
  if (current_payload_length == 0U || new_total_length > UINT16_MAX)
    return false;

  sequence->action = NPU_TUNNEL_REASSEMBLY_COMPLETE_IPV4;
  sequence->pending_length_offset =
      (uint16_t)(pending->layer_2_offset +
                 NPU_TUNNEL_REASSEMBLY_IPV4_TOTAL_LENGTH_OFFSET);
  sequence->pending_length = (uint16_t)new_total_length;
  sequence->pending_length_write = true;
  if (!operation_append(sequence, pending->packet_address,
                        pending->received_length, 0U, packet->channel, 0U, true,
                        false) ||
      !operation_append(sequence, packet->packet_address,
                        current_payload_length,
                        (uint32_t)decoded->layer_2_offset +
                            NPU_TUNNEL_REASSEMBLY_IPV4_PAYLOAD_OFFSET,
                        packet->channel, 0U, false, true))
    return false;
  return pppoe_patch_set(pending, decoded, packet->packet,
                         NPU_TUNNEL_REASSEMBLY_IPV4_PPPOE_OVERHEAD,
                         &sequence->operations[0]);
}

static bool
ipv6_plan(const struct npu_tunnel_reassembly_runtime *runtime,
          const struct npu_tunnel_reassembly_packet *packet,
          const struct decoded_packet *decoded,
          struct npu_tunnel_reassembly_operation_sequence *sequence) {
  const struct npu_tunnel_reassembly_pending *pending = &runtime->ipv6;
  uint32_t fragment_field_offset =
      (uint32_t)decoded->layer_2_offset +
      NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_FIELD_OFFSET;
  uint16_t fragment_field;
  uint16_t pending_payload_length;
  uint16_t current_payload_length;
  uint16_t current_fragment_payload_length;
  uint16_t fragment_offset;
  uint32_t new_payload_length;

  if (fragment_field_offset + NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_HEADER_SIZE >
      decoded->received_length)
    return false;
  if (packet->packet[decoded->layer_2_offset +
                     NPU_TUNNEL_REASSEMBLY_IPV6_NEXT_HEADER_OFFSET] !=
      NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_NEXT_HEADER)
    return false;

  fragment_field =
      npu_load_big_endian_u16(packet->packet + fragment_field_offset);
  sequence->packet_control = NPU_TUNNEL_REASSEMBLY_IPV6_CONTROL |
                             (uint32_t)packet->channel
                                 << NPU_TUNNEL_REASSEMBLY_PACKET_CHANNEL_SHIFT;
  if ((fragment_field & NPU_TUNNEL_REASSEMBLY_IPV6_MORE_FRAGMENTS) != 0U) {
    if (pending->active &&
        (!pending_valid(pending) || pending->packet == packet->packet))
      return false;
    sequence->action = NPU_TUNNEL_REASSEMBLY_STORE_IPV6;
    sequence->current_next_header_offset =
        (uint16_t)(decoded->layer_2_offset +
                   NPU_TUNNEL_REASSEMBLY_IPV6_NEXT_HEADER_OFFSET);
    sequence->current_next_header =
        packet->packet[decoded->layer_2_offset +
                       NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_NEXT_HEADER_OFFSET];
    sequence->current_next_header_write = true;
    sequence->replaces_pending = pending->active;
    return true;
  }

  if (!pending_valid(pending) || pending->channel != packet->channel ||
      pending->packet == packet->packet)
    return false;
  if ((size_t)pending->layer_2_offset +
          NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET >=
      pending->received_length)
    return false;
  if (npu_load_little_endian_u32(
          packet->packet + decoded->layer_2_offset +
          NPU_TUNNEL_REASSEMBLY_IPV6_IDENTIFICATION_OFFSET) !=
      npu_load_little_endian_u32(
          pending->packet + pending->layer_2_offset +
          NPU_TUNNEL_REASSEMBLY_IPV6_IDENTIFICATION_OFFSET))
    return false;

  pending_payload_length =
      npu_load_big_endian_u16(pending->packet + pending->layer_2_offset +
                              NPU_TUNNEL_REASSEMBLY_IPV6_PAYLOAD_LENGTH_OFFSET);
  fragment_offset =
      (uint16_t)(fragment_field & NPU_TUNNEL_REASSEMBLY_IPV6_OFFSET_MASK);
  if (pending_payload_length <
          NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_HEADER_SIZE ||
      fragment_offset != pending_payload_length -
                             NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_HEADER_SIZE)
    return false;
  if (decoded->received_length <=
      (uint32_t)decoded->layer_2_offset +
          NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET)
    return false;

  current_payload_length =
      (uint16_t)(decoded->received_length - decoded->layer_2_offset -
                 NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET);
  if (current_payload_length <= NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_HEADER_SIZE)
    return false;
  current_fragment_payload_length =
      (uint16_t)(current_payload_length -
                 NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_HEADER_SIZE);
  new_payload_length = pending_payload_length + current_fragment_payload_length;
  if (new_payload_length > UINT16_MAX)
    return false;

  sequence->action = NPU_TUNNEL_REASSEMBLY_COMPLETE_IPV6;
  sequence->pending_length_offset =
      (uint16_t)(pending->layer_2_offset +
                 NPU_TUNNEL_REASSEMBLY_IPV6_PAYLOAD_LENGTH_OFFSET);
  sequence->pending_length = (uint16_t)new_payload_length;
  sequence->pending_length_write = true;
  if (!operation_append(sequence, pending->packet_address,
                        (uint32_t)pending->layer_2_offset +
                            NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PREFIX_SIZE,
                        0U, packet->channel, 1U, true, false) ||
      !operation_append(sequence, pending->packet_address,
                        (uint32_t)pending->received_length -
                            pending->layer_2_offset -
                            NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET,
                        (uint32_t)pending->layer_2_offset +
                            NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET,
                        packet->channel, 0U, false, false) ||
      !operation_append(sequence, packet->packet_address,
                        current_payload_length,
                        (uint32_t)decoded->layer_2_offset +
                            NPU_TUNNEL_REASSEMBLY_IPV6_FRAGMENT_PAYLOAD_OFFSET,
                        packet->channel, 0U, false, true))
    return false;
  return pppoe_patch_set(pending, decoded, packet->packet,
                         NPU_TUNNEL_REASSEMBLY_IPV6_PPPOE_OVERHEAD,
                         &sequence->operations[0]);
}

bool npu_tunnel_reassembly_runtime_initialize(
    struct npu_tunnel_reassembly_runtime *runtime,
    npu_tunnel_reassembly_release_fn release, void *release_context) {
  if (runtime == NULL || release == NULL)
    return false;

  *runtime = (struct npu_tunnel_reassembly_runtime){
      .release = release,
      .release_context = release_context,
  };
  return true;
}

bool npu_tunnel_reassembly_operations_plan(
    const struct npu_tunnel_reassembly_runtime *runtime,
    const struct npu_tunnel_reassembly_packet *packet,
    struct npu_tunnel_reassembly_operation_sequence *sequence) {
  struct npu_tunnel_reassembly_operation_sequence decoded_sequence = {0};
  struct decoded_packet decoded;
  bool planned;

  if (runtime == NULL || sequence == NULL || runtime->release == NULL ||
      !packet_decode(packet, &decoded))
    return false;

  decoded_sequence.received_length = decoded.received_length;
  decoded_sequence.layer_2_offset = decoded.layer_2_offset;
  if (decoded.ip_version == NPU_TUNNEL_IP_VERSION_4)
    planned = ipv4_plan(runtime, packet, &decoded, &decoded_sequence);
  else
    planned = ipv6_plan(runtime, packet, &decoded, &decoded_sequence);
  if (!planned)
    return false;

  *sequence = decoded_sequence;
  return true;
}

static struct npu_tunnel_reassembly_pending *
pending_resolve(struct npu_tunnel_reassembly_runtime *runtime,
                enum npu_tunnel_reassembly_action action) {
  if (action == NPU_TUNNEL_REASSEMBLY_STORE_IPV4 ||
      action == NPU_TUNNEL_REASSEMBLY_COMPLETE_IPV4)
    return &runtime->ipv4;
  return &runtime->ipv6;
}

static void pending_clear(struct npu_tunnel_reassembly_pending *pending) {
  *pending = (struct npu_tunnel_reassembly_pending){0};
}

static void
pending_store(struct npu_tunnel_reassembly_pending *pending,
              const struct npu_tunnel_reassembly_packet *packet,
              const struct npu_tunnel_reassembly_operation_sequence *sequence) {
  *pending = (struct npu_tunnel_reassembly_pending){
      .packet = packet->packet,
      .packet_extent = packet->packet_extent,
      .packet_address = packet->packet_address,
      .received_length = sequence->received_length,
      .layer_2_offset = sequence->layer_2_offset,
      .channel = packet->channel,
      .active = true,
  };
}

static bool action_stores(enum npu_tunnel_reassembly_action action) {
  return action == NPU_TUNNEL_REASSEMBLY_STORE_IPV4 ||
         action == NPU_TUNNEL_REASSEMBLY_STORE_IPV6;
}

static bool
commands_encode(const struct npu_tunnel_reassembly_operation_sequence *sequence,
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

enum npu_runtime_result npu_tunnel_reassembly_execute(
    struct npu_tunnel_reassembly_runtime *runtime,
    const struct npu_tunnel_bridge_backend *bridge,
    const struct npu_tunnel_reassembly_packet *packet) {
  struct npu_tunnel_bridge_command
      commands[NPU_TUNNEL_REASSEMBLY_OPERATION_COUNT_MAX];
  struct npu_tunnel_reassembly_operation_sequence sequence;
  struct npu_tunnel_reassembly_pending *pending;
  enum npu_runtime_result result;
  uint32_t original_control;
  uint16_t original_current_fragment_field = 0U;
  uint16_t original_pending_length = 0U;
  uint8_t original_current_next_header = 0U;

  if (runtime == NULL || bridge == NULL || packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!npu_tunnel_reassembly_operations_plan(runtime, packet, &sequence) ||
      !commands_encode(&sequence, commands))
    return NPU_RUNTIME_REJECTED;

  pending = pending_resolve(runtime, sequence.action);
  if (action_stores(sequence.action) && sequence.replaces_pending) {
    result =
        runtime->release(runtime->release_context, pending->channel,
                         pending->packet_address, pending->received_length);
    if (result != NPU_RUNTIME_SUCCESS)
      return result;
    pending_clear(pending);
  }

  original_control = npu_load_little_endian_u32(packet->packet);
  if (sequence.current_fragment_field_write)
    original_current_fragment_field = npu_load_big_endian_u16(
        packet->packet + sequence.current_fragment_field_offset);
  if (sequence.current_next_header_write)
    original_current_next_header =
        packet->packet[sequence.current_next_header_offset];
  if (sequence.pending_length_write)
    original_pending_length = npu_load_big_endian_u16(
        pending->packet + sequence.pending_length_offset);

  npu_store_little_endian_u32(packet->packet, sequence.packet_control);
  if (sequence.current_fragment_field_write)
    npu_store_big_endian_u16(packet->packet +
                                 sequence.current_fragment_field_offset,
                             sequence.current_fragment_field);
  if (sequence.current_next_header_write)
    packet->packet[sequence.current_next_header_offset] =
        sequence.current_next_header;
  if (sequence.pending_length_write)
    npu_store_big_endian_u16(pending->packet + sequence.pending_length_offset,
                             sequence.pending_length);

  if (action_stores(sequence.action)) {
    pending_store(pending, packet, &sequence);
    return NPU_RUNTIME_SUCCESS;
  }

  result = npu_tunnel_bridge_commands_publish(bridge, packet->channel, commands,
                                              sequence.operation_count);
  if (result == NPU_RUNTIME_SUCCESS) {
    pending_clear(pending);
    return result;
  }

  npu_store_little_endian_u32(packet->packet, original_control);
  if (sequence.current_fragment_field_write)
    npu_store_big_endian_u16(packet->packet +
                                 sequence.current_fragment_field_offset,
                             original_current_fragment_field);
  if (sequence.current_next_header_write)
    packet->packet[sequence.current_next_header_offset] =
        original_current_next_header;
  if (sequence.pending_length_write)
    npu_store_big_endian_u16(pending->packet + sequence.pending_length_offset,
                             original_pending_length);
  return result;
}

static enum npu_runtime_result
pending_release(struct npu_tunnel_reassembly_runtime *runtime,
                struct npu_tunnel_reassembly_pending *pending) {
  enum npu_runtime_result result;

  if (!pending->active)
    return NPU_RUNTIME_SUCCESS;
  if (!pending_valid(pending))
    return NPU_RUNTIME_REJECTED;

  result = runtime->release(runtime->release_context, pending->channel,
                            pending->packet_address, pending->received_length);
  if (result == NPU_RUNTIME_SUCCESS)
    pending_clear(pending);
  return result;
}

enum npu_runtime_result
npu_tunnel_reassembly_flush(struct npu_tunnel_reassembly_runtime *runtime) {
  enum npu_runtime_result result;

  if (runtime == NULL || runtime->release == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  result = pending_release(runtime, &runtime->ipv4);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;
  return pending_release(runtime, &runtime->ipv6);
}
