/* SPDX-License-Identifier: GPL-2.0-only */
#include "an7581/services/tunnel/mailbox.h"

#include "an7581/runtime/endian.h"
#include "an7581/runtime/memory.h"

#define NPU_TUNNEL_BRIDGE_STORE_PREFIX_SIZE                                    \
  (NPU_TUNNEL_MAILBOX_HEADER_SIZE + 1U)
#define NPU_TUNNEL_BRIDGE_STORE_MESSAGE_SIZE                                   \
  (NPU_TUNNEL_BRIDGE_STORE_PREFIX_SIZE + NPU_TUNNEL_BRIDGE_CONTEXT_SIZE)
#define NPU_TUNNEL_U32_MESSAGE_SIZE                                            \
  (NPU_TUNNEL_MAILBOX_HEADER_SIZE + sizeof(uint32_t))
#define NPU_TUNNEL_FRAGMENT_MTU_MESSAGE_SIZE                                   \
  (NPU_TUNNEL_MAILBOX_HEADER_SIZE + 2U * sizeof(uint32_t))
#define NPU_TUNNEL_DEBUG_MESSAGE_SIZE                                          \
  (NPU_TUNNEL_MAILBOX_HEADER_SIZE + 2U * sizeof(uint32_t))
#define NPU_TUNNEL_LOCAL_SRV6_MESSAGE_SIZE                                     \
  (NPU_TUNNEL_MAILBOX_HEADER_SIZE + NPU_TUNNEL_LOCAL_SRV6_ADDRESS_SIZE)
#define NPU_TUNNEL_SRV6_STORE_PREFIX_SIZE (NPU_TUNNEL_MAILBOX_HEADER_SIZE + 2U)

static bool store_bridge_context(struct npu_tunnel_state *state,
                                 const uint8_t *message, size_t length) {
  struct npu_tunnel_bridge_context *context;
  uint32_t index;

  if (length < NPU_TUNNEL_BRIDGE_STORE_MESSAGE_SIZE)
    return false;

  index = message[NPU_TUNNEL_MAILBOX_HEADER_SIZE];
  if (index >= NPU_TUNNEL_BRIDGE_CONTEXT_COUNT)
    return false;

  context = &state->bridge_context[index];
  (void)npu_memcpy(context->data, message + NPU_TUNNEL_BRIDGE_STORE_PREFIX_SIZE,
                   sizeof(context->data));
  context->valid = true;
  return true;
}

static bool set_vxlan_mtu(struct npu_tunnel_state *state,
                          const uint8_t *message, size_t length) {
  uint32_t mtu;

  if (length < NPU_TUNNEL_U32_MESSAGE_SIZE)
    return false;

  mtu = npu_load_little_endian_u32(message + NPU_TUNNEL_MAILBOX_HEADER_SIZE);
  state->vxlan_mtu = mtu;
  state->vxlan_mtu_valid = true;
  return true;
}

static bool store_srv6_header(struct npu_tunnel_state *state,
                              const uint8_t *message, size_t length) {
  struct npu_tunnel_srv6_header *header;
  uint32_t index;
  size_t header_length;

  if (length < NPU_TUNNEL_SRV6_STORE_PREFIX_SIZE)
    return false;

  index = message[NPU_TUNNEL_MAILBOX_HEADER_SIZE];
  header_length = message[NPU_TUNNEL_MAILBOX_HEADER_SIZE + 1U];
  if (index >= NPU_TUNNEL_SRV6_HEADER_COUNT ||
      header_length > NPU_TUNNEL_SRV6_HEADER_CAPACITY ||
      header_length > length - NPU_TUNNEL_SRV6_STORE_PREFIX_SIZE)
    return false;

  header = &state->srv6_header[index];
  (void)npu_memcpy(header->data, message + NPU_TUNNEL_SRV6_STORE_PREFIX_SIZE,
                   header_length);
  header->length = (uint8_t)header_length;
  header->valid = true;
  return true;
}

static bool set_local_srv6_address(struct npu_tunnel_state *state,
                                   const uint8_t *message, size_t length) {
  if (length < NPU_TUNNEL_LOCAL_SRV6_MESSAGE_SIZE)
    return false;

  (void)npu_memcpy(state->local_srv6_address,
                   message + NPU_TUNNEL_MAILBOX_HEADER_SIZE,
                   sizeof(state->local_srv6_address));
  state->local_srv6_address_valid = true;
  return true;
}

static bool set_fragment_mtu(struct npu_tunnel_state *state,
                             const uint8_t *message, size_t length) {
  struct npu_tunnel_fragment_mtu *fragment_mtu;
  uint32_t index;

  if (length < NPU_TUNNEL_FRAGMENT_MTU_MESSAGE_SIZE)
    return false;

  index = message[NPU_TUNNEL_MAILBOX_HEADER_SIZE];
  if (index >= NPU_TUNNEL_FRAGMENT_MTU_COUNT)
    return false;

  fragment_mtu = &state->fragment_mtu[index];
  fragment_mtu->value = npu_load_little_endian_u32(
      message + NPU_TUNNEL_MAILBOX_HEADER_SIZE + sizeof(uint32_t));
  fragment_mtu->valid = true;
  return true;
}

static bool set_map_info_base(struct npu_tunnel_state *state,
                              const uint8_t *message, size_t length) {
  if (length < NPU_TUNNEL_U32_MESSAGE_SIZE)
    return false;

  state->map_info_base =
      npu_load_little_endian_u32(message + NPU_TUNNEL_MAILBOX_HEADER_SIZE);
  state->map_info_base_valid = true;
  return true;
}

static bool control_debug(struct npu_tunnel_state *state,
                          const uint8_t *message, size_t length) {
  uint32_t action;
  uint32_t reserved;

  if (length < NPU_TUNNEL_DEBUG_MESSAGE_SIZE)
    return false;

  action = npu_load_little_endian_u32(message + NPU_TUNNEL_MAILBOX_HEADER_SIZE);
  reserved = npu_load_little_endian_u32(
      message + NPU_TUNNEL_MAILBOX_HEADER_SIZE + sizeof(uint32_t));

  switch (action) {
  case NPU_TUNNEL_DEBUG_DUMP_BRIDGE_COUNTERS:
    ++state->debug.bridge_dump_requests;
    break;
  case NPU_TUNNEL_DEBUG_ENABLE_BRIDGE:
    state->debug.bridge_enabled = true;
    break;
  case NPU_TUNNEL_DEBUG_FLUSH_REASSEMBLY:
    ++state->debug.reassembly_flush_requests;
    break;
  default:
    return false;
  }

  state->debug.last_action = action;
  state->debug.last_reserved = reserved;
  state->debug.last_request_valid = true;
  return true;
}

static bool record_result(struct npu_tunnel_state *state, bool success) {
  ++state->decoded_requests;
  if (success)
    ++state->successful_requests;
  else
    ++state->rejected_requests;
  return success;
}

bool npu_tunnel_mailbox_handle(struct npu_tunnel_state *state, void *buffer,
                               size_t length) {
  const uint8_t *message = buffer;
  uint32_t command;

  if (state == NULL)
    return false;
  if (buffer == NULL || length < NPU_TUNNEL_MAILBOX_HEADER_SIZE) {
    ++state->invalid_requests;
    return false;
  }

  command = npu_load_little_endian_u32(message);
  state->last_command = command;
  state->last_command_valid = true;

  switch (command) {
  case NPU_TUNNEL_STORE_BRIDGE_CONTEXT:
    return record_result(state, store_bridge_context(state, message, length));
  case NPU_TUNNEL_NOOP:
    return record_result(state, true);
  case NPU_TUNNEL_SET_VXLAN_MTU:
    return record_result(state, set_vxlan_mtu(state, message, length));
  case NPU_TUNNEL_STORE_SRV6_HEADER:
    return record_result(state, store_srv6_header(state, message, length));
  case NPU_TUNNEL_SET_LOCAL_SRV6_ADDRESS:
    return record_result(state, set_local_srv6_address(state, message, length));
  case NPU_TUNNEL_SET_FRAGMENT_MTU:
    return record_result(state, set_fragment_mtu(state, message, length));
  case NPU_TUNNEL_DEBUG_CONTROL:
    return record_result(state, control_debug(state, message, length));
  case NPU_TUNNEL_SET_MAP_INFO_BASE:
    return record_result(state, set_map_info_base(state, message, length));
  default:
    return record_result(state, false);
  }
}
