/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/bridge.h"

#include "an7581/runtime/endian.h"
#include "an7581/services/tunnel/packet.h"

#define NPU_TUNNEL_BRIDGE_OPERATION_SHIFT 24U
#define NPU_TUNNEL_BRIDGE_BEGIN UINT32_C(0x80000000)
#define NPU_TUNNEL_BRIDGE_END UINT32_C(0x40000000)
#define NPU_TUNNEL_BRIDGE_PATCH_CONTROL UINT32_C(0xc0000000)

static bool
packet_region_valid(const struct npu_tunnel_bridge_packet_region *region) {
  uint64_t region_end;

  if (region == NULL || region->mapped_base == NULL ||
      region->mapped_extent == 0U ||
      (uint64_t)region->mapped_extent > UINT32_MAX ||
      (region->packet_address_base &
       ~(NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_MASK |
         NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_ALIAS)) != 0U ||
      (region->packet_address_base & NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_ALIAS) ==
          0U)
    return false;

  region_end = (uint64_t)region->packet_address_base + region->mapped_extent;
  return region_end <= (uint64_t)UINT32_MAX + 1U;
}

bool npu_tunnel_bridge_packet_region_initialize(
    struct npu_tunnel_bridge_packet_region *region, uint8_t *mapped_base,
    size_t mapped_extent, uint32_t packet_address_base) {
  struct npu_tunnel_bridge_packet_region initialized = {
      .mapped_base = mapped_base,
      .mapped_extent = mapped_extent,
      .packet_address_base = packet_address_base,
  };

  if (region == NULL || !packet_region_valid(&initialized))
    return false;

  *region = initialized;
  return true;
}

enum npu_runtime_result
npu_tunnel_bridge_packet_region_resolve(void *context, uint32_t packet_address,
                                        uint8_t **packet, size_t *extent) {
  const struct npu_tunnel_bridge_packet_region *region = context;
  size_t offset;

  if (region == NULL || packet == NULL || extent == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!packet_region_valid(region))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (packet_address < region->packet_address_base)
    return NPU_RUNTIME_OUT_OF_RANGE;

  offset = packet_address - region->packet_address_base;
  if (offset >= region->mapped_extent)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *packet = region->mapped_base + offset;
  *extent = region->mapped_extent - offset;
  return NPU_RUNTIME_SUCCESS;
}

static bool
ingress_backend_valid(const struct npu_tunnel_bridge_ingress_backend *backend) {
  return backend != NULL && backend->channel_status != NULL &&
         backend->channel_status_word_count >=
             NPU_TUNNEL_BRIDGE_CHANNEL_COUNT &&
         backend->packet_addresses != NULL &&
         backend->packet_address_stride_words != 0U &&
         backend->resolve != NULL &&
         backend->packet_address_alias ==
             NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_ALIAS;
}

bool npu_tunnel_bridge_ingress_initialize(
    struct npu_tunnel_bridge_ingress *ingress,
    const struct npu_tunnel_bridge_ingress_backend *backend) {
  struct npu_tunnel_bridge_ingress initialized = {0};
  size_t last_address_index;

  if (ingress == NULL || !ingress_backend_valid(backend))
    return false;

  last_address_index = (NPU_TUNNEL_BRIDGE_CHANNEL_COUNT - 1U) *
                       backend->packet_address_stride_words;
  if (last_address_index >= backend->packet_address_word_count)
    return false;

  initialized.backend = *backend;
  *ingress = initialized;
  return true;
}

enum npu_runtime_result npu_tunnel_bridge_ingress_dequeue(
    struct npu_tunnel_bridge_ingress *ingress, uint8_t channel,
    struct npu_tunnel_bridge_ingress_packet *packet) {
  struct npu_tunnel_bridge_ingress_packet decoded = {0};
  enum npu_runtime_result result;
  size_t address_index;
  size_t packet_extent = 0U;
  uint32_t packet_address;
  uint32_t raw_address;
  uint8_t *packet_data = NULL;
  uint8_t count;

  if (ingress == NULL || packet == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!ingress_backend_valid(&ingress->backend))
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT)
    return NPU_RUNTIME_OUT_OF_RANGE;

  address_index =
      (size_t)channel * ingress->backend.packet_address_stride_words;
  if (address_index >= ingress->backend.packet_address_word_count)
    return NPU_RUNTIME_OUT_OF_RANGE;

  count = ingress->cached_rx_count[channel];
  if (count == 0U) {
    count = (uint8_t)(ingress->backend.channel_status[channel] & UINT8_MAX);
    if (count == 0U)
      return NPU_RUNTIME_EMPTY;
    ingress->cached_rx_count[channel] = count;
  }

  raw_address = ingress->backend.packet_addresses[address_index];
  --ingress->cached_rx_count[channel];
  if ((raw_address & ~NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_MASK) != 0U)
    return NPU_RUNTIME_REJECTED;
  packet_address = raw_address | ingress->backend.packet_address_alias;

  result =
      ingress->backend.resolve(ingress->backend.resolve_context, packet_address,
                               &packet_data, &packet_extent);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;
  if (packet_data == NULL || packet_extent < sizeof(uint32_t))
    return NPU_RUNTIME_OUT_OF_RANGE;

  decoded = (struct npu_tunnel_bridge_ingress_packet){
      .packet = packet_data,
      .packet_extent = packet_extent,
      .packet_address = packet_address,
      .received_length = (uint32_t)npu_load_little_endian_u16(packet_data) +
                         NPU_TUNNEL_PACKET_CONTROL_SIZE,
      .channel = channel,
  };
  *packet = decoded;
  return NPU_RUNTIME_SUCCESS;
}

static enum npu_runtime_result
bridge_command_slot_resolve(const struct npu_tunnel_bridge_backend *backend,
                            uint8_t channel, volatile uint32_t **slot) {
  size_t slot_index;

  if (backend == NULL || slot == NULL || backend->channel_status == NULL ||
      backend->command_words == NULL || backend->barrier == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      backend->channel_status_word_count < NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      backend->command_stride_words < NPU_TUNNEL_BRIDGE_COMMAND_STRIDE_WORDS)
    return NPU_RUNTIME_OUT_OF_RANGE;

  slot_index = (size_t)channel * backend->command_stride_words;
  if (slot_index > backend->command_word_count ||
      NPU_TUNNEL_BRIDGE_COMMAND_WORD_COUNT >
          backend->command_word_count - slot_index)
    return NPU_RUNTIME_OUT_OF_RANGE;

  *slot = backend->command_words + slot_index;
  return NPU_RUNTIME_SUCCESS;
}

static void bridge_command_publish_unchecked(
    const struct npu_tunnel_bridge_backend *backend, volatile uint32_t *slot,
    const struct npu_tunnel_bridge_command *command) {
  size_t word_index;

  for (word_index = 1U; word_index < NPU_TUNNEL_BRIDGE_COMMAND_WORD_COUNT;
       ++word_index)
    slot[word_index] = command->words[word_index];
  backend->barrier(backend->barrier_context);
  slot[0] = command->words[0];
}

bool npu_tunnel_packet_operation_set(
    struct npu_tunnel_packet_operation *operation, uint32_t packet_address,
    uint32_t length, uint32_t source_offset, uint8_t channel,
    uint8_t operation_type, bool begin, bool end) {
  if (operation == NULL || length > UINT16_MAX || source_offset > UINT16_MAX ||
      channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT || operation_type >= 8U)
    return false;

  *operation = (struct npu_tunnel_packet_operation){
      .packet_address = packet_address,
      .length = (uint16_t)length,
      .source_offset = (uint16_t)source_offset,
      .channel = channel,
      .operation = operation_type,
      .begin = begin,
      .end = end,
  };
  return true;
}

uint32_t npu_tunnel_bridge_patch_u16(uint16_t offset, uint16_t value) {
  uint16_t wire_value =
      (uint16_t)((uint16_t)(value << 8) | (uint16_t)(value >> 8));

  return NPU_TUNNEL_BRIDGE_PATCH_CONTROL | (uint32_t)offset << 16 | wire_value;
}

bool npu_tunnel_packet_operation_encode(
    const struct npu_tunnel_packet_operation *operation,
    struct npu_tunnel_bridge_command *command) {
  struct npu_tunnel_bridge_command encoded = {0};
  uint32_t control;

  if (operation == NULL || command == NULL ||
      operation->channel >= NPU_TUNNEL_BRIDGE_CHANNEL_COUNT ||
      operation->operation >= 8U)
    return false;

  control = (uint32_t)operation->operation << NPU_TUNNEL_BRIDGE_OPERATION_SHIFT;
  if (operation->begin)
    control |= NPU_TUNNEL_BRIDGE_BEGIN;
  if (operation->end)
    control |= NPU_TUNNEL_BRIDGE_END;

  encoded.words[0] =
      operation->packet_address & NPU_TUNNEL_BRIDGE_PACKET_ADDRESS_MASK;
  encoded.words[1] =
      (uint32_t)operation->length << 16 | operation->source_offset;
  encoded.words[2] = control | operation->channel;
  encoded.words[3] = operation->modifiers[0];
  encoded.words[4] = operation->modifiers[1];
  encoded.words[5] = operation->modifiers[2];
  encoded.words[6] = operation->modifiers[3];
  *command = encoded;
  return true;
}

enum npu_runtime_result npu_tunnel_bridge_command_publish(
    const struct npu_tunnel_bridge_backend *backend, uint8_t channel,
    const struct npu_tunnel_bridge_command *command) {
  return npu_tunnel_bridge_commands_publish(backend, channel, command, 1U);
}

enum npu_runtime_result npu_tunnel_bridge_commands_publish(
    const struct npu_tunnel_bridge_backend *backend, uint8_t channel,
    const struct npu_tunnel_bridge_command *commands, size_t command_count) {
  enum npu_runtime_result result;
  volatile uint32_t *slot;
  size_t command_index;
  uint32_t tx_credit_count;

  if (commands == NULL || command_count == 0U)
    return NPU_RUNTIME_INVALID_ARGUMENT;

  result = bridge_command_slot_resolve(backend, channel, &slot);
  if (result != NPU_RUNTIME_SUCCESS)
    return result;

  tx_credit_count =
      (backend->channel_status[channel] & NPU_TUNNEL_BRIDGE_TX_CREDIT_MASK) >>
      NPU_TUNNEL_BRIDGE_TX_CREDIT_SHIFT;
  if (command_count > tx_credit_count)
    return NPU_RUNTIME_REJECTED;

  for (command_index = 0U; command_index < command_count; ++command_index)
    bridge_command_publish_unchecked(backend, slot, &commands[command_index]);
  return NPU_RUNTIME_SUCCESS;
}

enum npu_runtime_result npu_tunnel_bridge_packet_release(
    const struct npu_tunnel_bridge_backend *backend, uint8_t channel,
    uint32_t packet_address, uint16_t received_length) {
  struct npu_tunnel_packet_operation operation;
  struct npu_tunnel_bridge_command command;

  if (backend == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  if (!npu_tunnel_packet_operation_set(&operation, packet_address,
                                       received_length, 0U, channel, 2U, true,
                                       true) ||
      !npu_tunnel_packet_operation_encode(&operation, &command))
    return NPU_RUNTIME_OUT_OF_RANGE;
  return npu_tunnel_bridge_command_publish(backend, channel, &command);
}

enum npu_runtime_result
npu_tunnel_bridge_packet_release_callback(void *context, uint8_t channel,
                                          uint32_t packet_address,
                                          uint16_t received_length) {
  if (context == NULL)
    return NPU_RUNTIME_INVALID_ARGUMENT;
  return npu_tunnel_bridge_packet_release(context, channel, packet_address,
                                          received_length);
}
