/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/fragment.h"

#include "an7581/runtime/endian.h"

#define NPU_TUNNEL_FRAGMENT_IPV4_PACKET_CONTROL UINT32_C(0x00002200)
#define NPU_TUNNEL_FRAGMENT_IPV6_PACKET_CONTROL UINT32_C(0x00000200)
#define NPU_TUNNEL_FRAGMENT_PACKET_CHANNEL_SHIFT 4U

static bool pppoe_offset_valid(uint8_t offset) {
  return offset == 0U || offset == NPU_TUNNEL_PPPOE_LENGTH_OFFSET_DIRECT ||
         offset == NPU_TUNNEL_PPPOE_LENGTH_OFFSET_VLAN;
}

static uint32_t pppoe_length_patch(uint8_t offset, uint16_t ip_length) {
  if (offset == 0U)
    return 0U;
  return npu_tunnel_bridge_patch_u16(offset, (uint16_t)(ip_length + 2U));
}

static bool plan_ipv4(const struct npu_tunnel_fragment_plan *plan,
                      const struct npu_tunnel_fragment_operation_config *config,
                      struct npu_tunnel_fragment_operation_sequence *sequence) {
  struct npu_tunnel_packet_operation *operations = sequence->operations;
  const struct npu_tunnel_fragment_segment *first =
      &sequence->layout.segments[0];
  const struct npu_tunnel_fragment_segment *second =
      &sequence->layout.segments[1];
  uint16_t first_ip_length = (uint16_t)(sequence->layout.network_header_length +
                                        first->payload_length);
  uint16_t second_ip_length =
      (uint16_t)(sequence->layout.network_header_length +
                 second->payload_length);
  uint32_t header_length = NPU_TUNNEL_PACKET_CONTROL_SIZE +
                           plan->layer_2_offset +
                           sequence->layout.network_header_length;
  uint32_t second_payload_offset = header_length + first->payload_length;

  if (config->pppoe_length_offset != 0U &&
      (first_ip_length > UINT16_MAX - 2U || second_ip_length > UINT16_MAX - 2U))
    return false;
  if (!npu_tunnel_packet_operation_set(&operations[0], config->packet_address,
                                       NPU_TUNNEL_PACKET_CONTROL_SIZE +
                                           first->wire_length,
                                       0U, config->channel, 1U, true, true) ||
      !npu_tunnel_packet_operation_set(&operations[1], config->packet_address,
                                       header_length, 0U, config->channel, 1U,
                                       true, false) ||
      !npu_tunnel_packet_operation_set(
          &operations[2], config->packet_address, second->payload_length,
          second_payload_offset, config->channel, 0U, false, true))
    return false;

  operations[0].modifiers[0] = npu_tunnel_bridge_patch_u16(
      (uint16_t)(plan->layer_2_offset + 0x22U), first_ip_length);
  operations[0].modifiers[1] = npu_tunnel_bridge_patch_u16(
      (uint16_t)(plan->layer_2_offset + 0x26U), UINT16_C(0x2000));
  operations[0].modifiers[2] =
      pppoe_length_patch(config->pppoe_length_offset, first_ip_length);
  operations[1].modifiers[0] = npu_tunnel_bridge_patch_u16(
      (uint16_t)(plan->layer_2_offset + 0x22U), second_ip_length);
  operations[1].modifiers[1] =
      npu_tunnel_bridge_patch_u16((uint16_t)(plan->layer_2_offset + 0x26U),
                                  (uint16_t)(first->payload_length / 8U));
  operations[1].modifiers[2] =
      pppoe_length_patch(config->pppoe_length_offset, second_ip_length);
  sequence->operation_count = 3U;
  return true;
}

static void set_ipv6_fragment_header_modifiers(
    struct npu_tunnel_packet_operation *operation, uint8_t next_header,
    uint16_t fragment_field, uint32_t identification) {
  operation->modifiers[0] =
      npu_tunnel_bridge_patch_u16(0U, (uint16_t)((uint16_t)next_header << 8));
  operation->modifiers[1] = npu_tunnel_bridge_patch_u16(2U, fragment_field);
  operation->modifiers[2] =
      npu_tunnel_bridge_patch_u16(4U, (uint16_t)(identification >> 16));
  operation->modifiers[3] =
      npu_tunnel_bridge_patch_u16(6U, (uint16_t)identification);
}

static bool plan_ipv6(const struct npu_tunnel_fragment_plan *plan,
                      const struct npu_tunnel_fragment_operation_config *config,
                      struct npu_tunnel_fragment_operation_sequence *sequence) {
  struct npu_tunnel_packet_operation *operations = sequence->operations;
  const struct npu_tunnel_fragment_segment *first =
      &sequence->layout.segments[0];
  const struct npu_tunnel_fragment_segment *second =
      &sequence->layout.segments[1];
  uint16_t first_ipv6_payload_length =
      (uint16_t)(sequence->layout.inserted_fragment_header_length +
                 first->payload_length);
  uint16_t second_ipv6_payload_length =
      (uint16_t)(sequence->layout.inserted_fragment_header_length +
                 second->payload_length);
  uint16_t first_ip_length = (uint16_t)(sequence->layout.network_header_length +
                                        first_ipv6_payload_length);
  uint16_t second_ip_length =
      (uint16_t)(sequence->layout.network_header_length +
                 second_ipv6_payload_length);
  uint32_t payload_offset = NPU_TUNNEL_PACKET_CONTROL_SIZE +
                            plan->layer_2_offset +
                            sequence->layout.network_header_length;
  uint32_t second_payload_offset = payload_offset + first->payload_length;

  if (config->ipv6_fragment_header_address == 0U ||
      (config->pppoe_length_offset != 0U &&
       (first_ip_length > UINT16_MAX - 2U ||
        second_ip_length > UINT16_MAX - 2U)))
    return false;
  if (!npu_tunnel_packet_operation_set(&operations[0], config->packet_address,
                                       payload_offset, 0U, config->channel, 1U,
                                       true, false) ||
      !npu_tunnel_packet_operation_set(&operations[1],
                                       config->ipv6_fragment_header_address, 8U,
                                       0U, config->channel, 1U, false, false) ||
      !npu_tunnel_packet_operation_set(&operations[2], config->packet_address,
                                       first->payload_length, payload_offset,
                                       config->channel, 1U, false, true) ||
      !npu_tunnel_packet_operation_set(&operations[3], config->packet_address,
                                       payload_offset, 0U, config->channel, 1U,
                                       true, false) ||
      !npu_tunnel_packet_operation_set(&operations[4],
                                       config->ipv6_fragment_header_address, 8U,
                                       0U, config->channel, 1U, false, false) ||
      !npu_tunnel_packet_operation_set(
          &operations[5], config->packet_address, second->payload_length,
          second_payload_offset, config->channel, 0U, false, true))
    return false;

  operations[0].modifiers[0] = npu_tunnel_bridge_patch_u16(
      (uint16_t)(plan->layer_2_offset + 0x24U), first_ipv6_payload_length);
  operations[0].modifiers[1] =
      pppoe_length_patch(config->pppoe_length_offset, first_ip_length);
  set_ipv6_fragment_header_modifiers(&operations[1], config->ipv6_next_header,
                                     1U, config->ipv6_identification);
  operations[3].modifiers[0] = npu_tunnel_bridge_patch_u16(
      (uint16_t)(plan->layer_2_offset + 0x24U), second_ipv6_payload_length);
  operations[3].modifiers[1] =
      pppoe_length_patch(config->pppoe_length_offset, second_ip_length);
  set_ipv6_fragment_header_modifiers(&operations[4], config->ipv6_next_header,
                                     first->payload_length,
                                     config->ipv6_identification);

  sequence->operation_count = 6U;
  sequence->ipv6_next_header_offset =
      (uint16_t)(NPU_TUNNEL_PACKET_CONTROL_SIZE + plan->layer_2_offset + 6U);
  sequence->ipv6_next_header_value = NPU_TUNNEL_IPV6_FRAGMENT_NEXT_HEADER;
  sequence->ipv6_next_header_write = true;
  return true;
}

bool npu_tunnel_fragment_operations_plan(
    const struct npu_tunnel_fragment_plan *plan,
    const struct npu_tunnel_fragment_operation_config *config,
    struct npu_tunnel_fragment_operation_sequence *sequence) {
  struct npu_tunnel_fragment_operation_sequence decoded = {0};
  bool planned;

  if (plan == NULL || config == NULL || sequence == NULL ||
      config->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      !pppoe_offset_valid(config->pppoe_length_offset))
    return false;
  if (!npu_tunnel_fragment_layout(plan, &decoded.layout))
    return false;

  if (plan->ip_version == NPU_TUNNEL_IP_VERSION_4)
    planned = plan_ipv4(plan, config, &decoded);
  else
    planned = plan_ipv6(plan, config, &decoded);
  if (!planned)
    return false;

  *sequence = decoded;
  return true;
}

static enum npu_runtime_result fragment_execution_validate(
    const struct npu_tunnel_fragment_plan *plan,
    const struct npu_tunnel_fragment_execution *execution) {
  size_t required_extent;

  if (plan == NULL || execution == NULL || execution->packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (execution->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      !pppoe_offset_valid(execution->pppoe_length_offset))
    return NPU_RUNTIME_OUT_OF_RANGE;

  required_extent =
      NPU_TUNNEL_PACKET_CONTROL_SIZE + (size_t)plan->packet_length;
  if (execution->packet_extent < required_extent ||
      npu_load_little_endian_u16(execution->packet) != plan->packet_length)
    return NPU_RUNTIME_OUT_OF_RANGE;
  return NPU_RUNTIME_SUCCESS;
}

static bool fragment_commands_encode(
    const struct npu_tunnel_fragment_operation_sequence *sequence,
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

static uint32_t fragment_packet_control(enum npu_tunnel_ip_version ip_version,
                                        uint8_t channel) {
  uint32_t control = (uint32_t)channel
                     << NPU_TUNNEL_FRAGMENT_PACKET_CHANNEL_SHIFT;

  if (ip_version == NPU_TUNNEL_IP_VERSION_4)
    return control | NPU_TUNNEL_FRAGMENT_IPV4_PACKET_CONTROL;
  return control | NPU_TUNNEL_FRAGMENT_IPV6_PACKET_CONTROL;
}

enum npu_runtime_result npu_tunnel_fragment_execute(
    struct npu_tunnel_fragment_runtime *runtime,
    const struct npu_tunnel_bridge_backend *bridge,
    const struct npu_tunnel_fragment_plan *plan,
    const struct npu_tunnel_fragment_execution *execution) {
  struct npu_tunnel_bridge_command
      commands[NPU_TUNNEL_FRAGMENT_OPERATION_COUNT_MAX];
  struct npu_tunnel_fragment_operation_config config = {0};
  struct npu_tunnel_fragment_operation_sequence sequence;
  enum npu_runtime_result result;
  uint32_t identification = 0U;
  uint32_t original_control;
  uint8_t original_next_header = 0U;

  if (runtime == NULL || bridge == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  result = fragment_execution_validate(plan, execution);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;

  config.packet_address = execution->packet_address;
  config.ipv6_fragment_header_address = execution->ipv6_fragment_header_address;
  config.channel = execution->channel;
  config.pppoe_length_offset = execution->pppoe_length_offset;
  if (plan->ip_version == NPU_TUNNEL_IP_VERSION_6) {
    size_t next_header_offset =
        NPU_TUNNEL_PACKET_CONTROL_SIZE + (size_t)plan->layer_2_offset + 6U;

    if (next_header_offset >= execution->packet_extent)
      return NPU_RUNTIME_OUT_OF_RANGE;
    original_next_header = execution->packet[next_header_offset];
    identification = runtime->ipv6_identification + 1U;
    config.ipv6_next_header = original_next_header;
    config.ipv6_identification = identification;
  }

  if (!npu_tunnel_fragment_operations_plan(plan, &config, &sequence) ||
      !fragment_commands_encode(&sequence, commands))
    return NPU_RUNTIME_REJECTED;

  original_control = npu_load_little_endian_u32(execution->packet);
  npu_store_little_endian_u32(
      execution->packet,
      fragment_packet_control(plan->ip_version, execution->channel));
  if (sequence.ipv6_next_header_write)
    execution->packet[sequence.ipv6_next_header_offset] =
        sequence.ipv6_next_header_value;

  result = npu_tunnel_bridge_commands_publish(
      bridge, execution->channel, commands, sequence.operation_count);
  if (result != NPU_RUNTIME_SUCCESS) {
    npu_store_little_endian_u32(execution->packet, original_control);
    if (sequence.ipv6_next_header_write)
      execution->packet[sequence.ipv6_next_header_offset] =
          original_next_header;
    return result;
  }

  if (plan->ip_version == NPU_TUNNEL_IP_VERSION_6)
    runtime->ipv6_identification = identification;
  return NPU_RUNTIME_SUCCESS;
}
